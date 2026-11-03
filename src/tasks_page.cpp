#include "tasks_page.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace focus {
TasksPage::TasksPage(Application& app) : app_(app) {
    root_ = ui::box(GTK_ORIENTATION_VERTICAL);
    auto* toolbar = ui::box(GTK_ORIENTATION_HORIZONTAL,12);
    gtk_widget_add_css_class(toolbar,"page-toolbar");
    show_done_ = gtk_check_button_new_with_label(_("Show Completed"));
    gtk_box_append(GTK_BOX(toolbar),show_done_);
    auto* spacer = ui::box(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(spacer,true);
    gtk_box_append(GTK_BOX(toolbar),spacer);
    auto* add = ui::button(_("New Task"),"suggested-action");
    gtk_box_append(GTK_BOX(toolbar),add);
    gtk_box_append(GTK_BOX(root_),toolbar);
    auto* content = ui::box(GTK_ORIENTATION_VERTICAL);
    task_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(task_list_),GTK_SELECTION_NONE);
    gtk_widget_add_css_class(task_list_,"boxed-list");
    task_empty_ = ui::empty_state("view-list-symbolic",_("No Tasks"));
    gtk_box_append(GTK_BOX(content),task_list_);
    gtk_box_append(GTK_BOX(content),task_empty_);
    gtk_box_append(GTK_BOX(root_),ui::page(content));
    ui::clicked(add,[this]{edit_task();});
    ui::on(G_OBJECT(show_done_),"toggled",root_,[this]{reload();});
    reload();
}
void TasksPage::reload() {
    const auto tasks = app_.store().tasks();
    task_rows_.clear();
    ui::clear_list(task_list_);
    int shown = 0;
    for (const auto& task : tasks) {
        if (task.done && !gtk_check_button_get_active(GTK_CHECK_BUTTON(show_done_))) continue;
        ++shown;
        auto* row = adw_action_row_new();
        task_rows_[task.id] = ADW_ACTION_ROW(row);
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row),false);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),task.title.c_str());
        adw_action_row_set_title_lines(ADW_ACTION_ROW(row),1);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row),i18n::format(_("Total focus %s"),duration_text(task.total).c_str()).c_str());
        auto* check = gtk_check_button_new();
        gtk_widget_set_tooltip_text(check,task.done?_("Reopen task"):_("Mark as completed"));
        gtk_check_button_set_active(GTK_CHECK_BUTTON(check),task.done);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row),check);
        auto* edit = ui::icon_button("document-edit-symbolic",_("Edit task name"));
        gtk_widget_add_css_class(edit,"flat"); adw_action_row_add_suffix(ADW_ACTION_ROW(row),edit);
        ui::clicked(edit,[this,task]{edit_task(&task);});
        ui::on(G_OBJECT(check),"toggled",root_,[this,task,check] {
            const bool done = gtk_check_button_get_active(GTK_CHECK_BUTTON(check));
            if (done==task.done) return;
            if (task.id==app_.selected_task() && app_.timer().engaged()) {
                // Restore the control before reporting the rejected change.
                gtk_check_button_set_active(GTK_CHECK_BUTTON(check),task.done);
                app_.toast(_("Finish this task’s timer first."));
                return;
            }
            app_.store().update_task(task.id,task.title,done);
            app_.tasks_changed();
        });
        gtk_list_box_append(GTK_LIST_BOX(task_list_),row);
    }
    gtk_widget_set_visible(task_list_,shown>0);
    gtk_widget_set_visible(task_empty_,shown==0);
}
void TasksPage::refresh_totals() {
    for (const auto& task : app_.store().tasks()) {
        if (auto row = task_rows_.find(task.id); row != task_rows_.end())
            adw_action_row_set_subtitle(row->second,i18n::format(_("Total focus %s"),duration_text(task.total).c_str()).c_str());
    }
}
void TasksPage::edit_task(const Task* task) {
    auto* dialog = adw_alert_dialog_new(task?_("Edit Task"):_("New Long-term Task"),nullptr);
    auto* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry),_("Task name"));
    gtk_entry_set_max_length(GTK_ENTRY(entry),120);
    gtk_entry_set_activates_default(GTK_ENTRY(entry),true);
    if (task) gtk_editable_set_text(GTK_EDITABLE(entry),task->title.c_str());
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog),entry);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),"cancel",_("Cancel"),"save",task?_("Save"):_("Create"),nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog),"save",ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog),"save");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog),"cancel");
    adw_alert_dialog_set_response_enabled(ADW_ALERT_DIALOG(dialog),"save",task!=nullptr);
    ui::on(G_OBJECT(entry),"changed",GTK_WIDGET(dialog),[dialog,entry] {
        adw_alert_dialog_set_response_enabled(ADW_ALERT_DIALOG(dialog),"save",!ui::trimmed(gtk_editable_get_text(GTK_EDITABLE(entry))).empty());
    });
    const auto saved = task ? std::optional<Task>(*task) : std::nullopt;
    ui::response(dialog,[this,entry,saved](const std::string& response) {
        if (response!="save") return;
        auto title = ui::trimmed(gtk_editable_get_text(GTK_EDITABLE(entry)));
        if (saved) app_.store().update_task(saved->id,title,saved->done);
        else {
            auto id = app_.store().add_task(title);
            if (!app_.timer().engaged()) app_.select_task(id);
        }
        app_.tasks_changed();
    });
    adw_dialog_present(dialog,app_.window());
    gtk_widget_grab_focus(entry);
}
}
