#include "stats_page.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include <algorithm>

namespace focus {
namespace {
std::string metric(Microseconds value) {
    if (value<60*second) return i18n::format(_("%lld s"),static_cast<long long>(value/second));
    if (value<3600*second) return i18n::format(_("%lld min"),static_cast<long long>(value/(60*second)));
    return i18n::format(_("%.1f h"),static_cast<double>(value)/(3600*second));
}
std::string timestamp(Microseconds value,const std::string& day = {}) {
    if (!value) return _("Not recorded");
    auto* date=g_date_time_new_from_unix_local(value/second);
    const auto date_text=local_date(value);
    const char* format=date_text==day?"%H:%M:%S":
        date_text.substr(0,4)==day.substr(0,4)?"%m-%d %H:%M:%S":"%Y-%m-%d %H:%M:%S";
    auto* text=g_date_time_format(date,format);
    std::string result(text);
    g_free(text); g_date_time_unref(date);
    return result;
}
}
StatsPage::StatsPage(Application& app) : app_(app) {
    auto* content=ui::box(GTK_ORIENTATION_VERTICAL,24);
    root_=adw_navigation_view_new();
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(root_),adw_navigation_page_new_with_tag(ui::page(content),_("Statistics"),"overview"));
    auto* cards=ui::box(GTK_ORIENTATION_HORIZONTAL,12);
    gtk_box_set_homogeneous(GTK_BOX(cards),true);
    auto metric=[&](const char* name) {
        auto* card=ui::box(GTK_ORIENTATION_VERTICAL,10);
        gtk_widget_add_css_class(card,"card"); gtk_widget_add_css_class(card,"stat-card");
        gtk_box_append(GTK_BOX(card),ui::label(name,"dim-label"));
        auto* value=ui::label("0", "stat-value");
        gtk_box_append(GTK_BOX(card),value); gtk_box_append(GTK_BOX(cards),card);
        return value;
    };
    today_=metric(_("Today's Focus")); week_=metric(_("Last 7 Days")); total_=metric(_("Total Focus")); completed_=metric(_("Pomodoros"));
    gtk_box_append(GTK_BOX(content),cards);
    auto* chart_card=ui::box(GTK_ORIENTATION_VERTICAL,18);
    gtk_widget_add_css_class(chart_card,"card"); gtk_widget_add_css_class(chart_card,"chart-card");
    gtk_box_append(GTK_BOX(chart_card),ui::label(_("Focus in the Last 7 Days"), "title-3"));
    auto* chart=ui::box(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_set_homogeneous(GTK_BOX(chart),true);
    for (size_t index=0;index<days_.size();++index) {
        auto& day=days_[index];
        day.button=gtk_button_new();
        gtk_widget_add_css_class(day.button,"flat");
        gtk_widget_add_css_class(day.button,"chart-day");
        gtk_widget_set_cursor_from_name(day.button,"pointer");
        auto* column=ui::box(GTK_ORIENTATION_VERTICAL,10);
        day.amount=ui::label("","chart-value");
        gtk_widget_set_halign(day.amount,GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(column),day.amount);
        auto* track=gtk_overlay_new(); gtk_widget_add_css_class(track,"bar-track");
        gtk_widget_set_size_request(track,-1,150);
        day.fill=ui::box(GTK_ORIENTATION_VERTICAL);
        gtk_widget_add_css_class(day.fill,"bar-fill");
        gtk_widget_set_valign(day.fill,GTK_ALIGN_END);
        gtk_overlay_set_child(GTK_OVERLAY(track),day.fill);
        gtk_box_append(GTK_BOX(column),track);
        day.date=ui::label("",index==6?"accent":"dim-label");
        gtk_widget_set_halign(day.date,GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(column),day.date);
        gtk_button_set_child(GTK_BUTTON(day.button),column);
        ui::clicked(day.button,[this,index]{show_day(days_[index].day);});
        gtk_box_append(GTK_BOX(chart),day.button);
    }
    gtk_box_append(GTK_BOX(chart_card),chart);
    gtk_box_append(GTK_BOX(content),chart_card);
    auto* task_box=ui::box(GTK_ORIENTATION_VERTICAL,12);
    gtk_box_append(GTK_BOX(task_box),ui::label(_("Focus by Task"), "title-3"));
    tasks_=gtk_list_box_new(); gtk_list_box_set_selection_mode(GTK_LIST_BOX(tasks_),GTK_SELECTION_NONE);
    gtk_widget_add_css_class(tasks_,"boxed-list");
    gtk_box_append(GTK_BOX(task_box),tasks_);
    empty_=ui::empty_state("view-statistics-symbolic",_("No Focus Records"));
    gtk_box_append(GTK_BOX(task_box),empty_);
    gtk_box_append(GTK_BOX(content),task_box);

    auto* detail_box=ui::box(GTK_ORIENTATION_VERTICAL);
    auto* toolbar=ui::box(GTK_ORIENTATION_HORIZONTAL,12);
    gtk_widget_add_css_class(toolbar,"page-toolbar");
    auto* back=ui::icon_button("go-previous-symbolic",_("Back to statistics"));
    gtk_widget_add_css_class(back,"flat");
    ui::clicked(back,[this]{adw_navigation_view_pop(ADW_NAVIGATION_VIEW(root_));});
    gtk_box_append(GTK_BOX(toolbar),back);
    records_title_=ui::label("","title-2");
    gtk_box_append(GTK_BOX(toolbar),records_title_);
    gtk_box_append(GTK_BOX(detail_box),toolbar);
    auto* record_content=ui::box(GTK_ORIENTATION_VERTICAL,16);
    records_total_=ui::label("","record-summary");
    gtk_box_append(GTK_BOX(record_content),records_total_);
    records_=gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(records_),GTK_SELECTION_NONE);
    gtk_widget_add_css_class(records_,"boxed-list");
    gtk_box_append(GTK_BOX(record_content),records_);
    records_empty_=ui::empty_state("view-statistics-symbolic",_("No Focus Records"));
    gtk_box_append(GTK_BOX(record_content),records_empty_);
    records_scroll_=ui::page(record_content);
    gtk_box_append(GTK_BOX(detail_box),records_scroll_);
    details_=adw_navigation_page_new(detail_box,_("Focus Records"));
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(root_),details_);
}
void StatsPage::show_overview() {
    const char* pages[]={"overview"};
    adw_navigation_view_replace_with_tags(ADW_NAVIGATION_VIEW(root_),pages,1);
}
void StatsPage::refresh() {
    const auto stats=app_.store().statistics();
    if (rendered_statistics_ && *rendered_statistics_==stats) {
        if (adw_navigation_view_get_visible_page(ADW_NAVIGATION_VIEW(root_))==details_) refresh_records();
        return;
    }
    gtk_label_set_text(GTK_LABEL(today_),metric(stats.today).c_str());
    gtk_label_set_text(GTK_LABEL(week_),metric(stats.week).c_str());
    gtk_label_set_text(GTK_LABEL(total_),metric(stats.all).c_str());
    gtk_label_set_text(GTK_LABEL(completed_),std::to_string(stats.completed).c_str());
    Microseconds maximum=60*second;
    for (const auto& day : stats.days) maximum=std::max(maximum,day.total);
    for (size_t index=0;index<stats.days.size();++index) {
        const auto& day=stats.days[index];
        auto& widgets=days_[index];
        widgets.day=day.date;
        gtk_label_set_text(GTK_LABEL(widgets.amount),metric(day.total).c_str());
        gtk_widget_set_size_request(widgets.fill,-1,day.total>0?std::max(3,static_cast<int>(150.0*static_cast<double>(day.total)/static_cast<double>(maximum))):0);
        gtk_widget_set_visible(widgets.fill,day.total>0);
        const auto description=i18n::format(_("%s · %s · View focus records"),day.date.c_str(),duration_text(day.total).c_str());
        gtk_widget_set_tooltip_text(widgets.button,description.c_str());
        gtk_accessible_update_property(GTK_ACCESSIBLE(widgets.button),GTK_ACCESSIBLE_PROPERTY_LABEL,description.c_str(),-1);
        gtk_label_set_text(GTK_LABEL(widgets.date),index==6?_("Today"):day.date.substr(5).c_str());
    }
    ui::clear_list(tasks_);
    for (const auto& task : stats.tasks) {
        auto* row=adw_action_row_new();
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row),false);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),task.title.c_str());
        adw_action_row_set_title_lines(ADW_ACTION_ROW(row),1);
        const auto percent=stats.all>0?static_cast<int>(100.0*static_cast<double>(task.total)/static_cast<double>(stats.all)):0;
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row),(std::to_string(percent)+"%").c_str());
        auto* duration=ui::label(duration_text(task.total),"task-total");
        gtk_label_set_wrap(GTK_LABEL(duration),false);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row),duration);
        gtk_list_box_append(GTK_LIST_BOX(tasks_),row);
    }
    gtk_widget_set_visible(tasks_,!stats.tasks.empty());
    gtk_widget_set_visible(empty_,stats.tasks.empty());
    rendered_statistics_=stats;
    if (adw_navigation_view_get_visible_page(ADW_NAVIGATION_VIEW(root_))==details_) refresh_records();
}
void StatsPage::show_day(const std::string& day) {
    selected_day_=day;
    gtk_label_set_text(GTK_LABEL(records_title_),i18n::format(_("Focus records for %s"),day.c_str()).c_str());
    refresh_records();
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(records_scroll_)),0);
    adw_navigation_view_push(ADW_NAVIGATION_VIEW(root_),details_);
}
void StatsPage::refresh_records() {
    const auto records=app_.store().sessions_on(selected_day_);
    // Keep widgets in place during timer updates so focus and scrolling remain stable.
    const bool same_rows=records.size()==record_rows_.size() && std::equal(records.begin(),records.end(),record_rows_.begin(),
        [](const SessionRecord& record,const RecordWidgets& widgets){return record.id==widgets.id;});
    if (!same_rows) {
        record_rows_.clear();
        ui::clear_list(records_);
        for (const auto& record : records) {
            auto* row=adw_action_row_new();
            gtk_widget_add_css_class(row,"record-row");
            adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row),false);
            adw_action_row_set_title_lines(ADW_ACTION_ROW(row),1);
            auto* durations=ui::box(GTK_ORIENTATION_VERTICAL,6);
            gtk_widget_set_valign(durations,GTK_ALIGN_CENTER);
            gtk_widget_add_css_class(durations,"record-durations");
            auto* duration=ui::label("","record-duration");
            gtk_label_set_wrap(GTK_LABEL(duration),false);
            gtk_label_set_xalign(GTK_LABEL(duration),1);
            gtk_box_append(GTK_BOX(durations),duration);
            auto* day_duration=ui::label("","record-day-duration");
            gtk_label_set_wrap(GTK_LABEL(day_duration),false);
            gtk_label_set_xalign(GTK_LABEL(day_duration),1);
            gtk_box_append(GTK_BOX(durations),day_duration);
            auto* remove=ui::icon_button("user-trash-symbolic",_("Delete focus record"));
            gtk_widget_add_css_class(remove,"flat");
            ui::clicked(remove,[this,id=record.id]{confirm_delete(id);});
            adw_action_row_add_suffix(ADW_ACTION_ROW(row),durations);
            adw_action_row_add_suffix(ADW_ACTION_ROW(row),remove);
            gtk_list_box_append(GTK_LIST_BOX(records_),row);
            record_rows_.push_back({record.id,ADW_ACTION_ROW(row),duration,day_duration});
        }
    }
    Microseconds total=0;
    for (size_t index=0;index<records.size();++index) {
        const auto& record=records[index];
        const auto& widgets=record_rows_[index];
        total+=record.day_total;
        const bool active=record.id==app_.active_session();
        const std::string state=active?(app_.timer().running()?_("Running"):_("Paused")):record.completed?_("Completed"):_("Ended");
        const std::string mode=record.mode==Mode::Pomodoro?_("Pomodoro"):_("Stopwatch");
        const auto range=!record.started_at && !record.ended_at && !active?std::string(_("Start and end times not recorded")):
            timestamp(record.started_at,selected_day_)+" — "+(active?state:timestamp(record.ended_at,selected_day_));
        const auto subtitle=range+"  ·  "+mode;
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widgets.row),record.task.c_str());
        adw_action_row_set_subtitle(widgets.row,subtitle.c_str());
        const auto description=i18n::format(_("%s · %s\nStart  %s\nEnd  %s"),mode.c_str(),state.c_str(),
            timestamp(record.started_at).c_str(),(active?state:timestamp(record.ended_at)).c_str());
        gtk_widget_set_tooltip_text(GTK_WIDGET(widgets.row),description.c_str());
        gtk_label_set_text(GTK_LABEL(widgets.duration),clock_text(record.total).c_str());
        gtk_widget_set_tooltip_text(widgets.duration,_("Focus duration, excluding pauses"));
        gtk_label_set_text(GTK_LABEL(widgets.day_duration),i18n::format(_("This day %s"),clock_text(record.day_total).c_str()).c_str());
        gtk_widget_set_visible(widgets.day_duration,record.total!=record.day_total);
    }
    gtk_label_set_text(GTK_LABEL(records_total_),i18n::format(P_("%zu record · Focus time %s",
        "%zu records · Focus time %s",records.size()),records.size(),clock_text(total).c_str()).c_str());
    gtk_widget_set_visible(records_,!records.empty());
    gtk_widget_set_visible(records_total_,!records.empty());
    gtk_widget_set_visible(records_empty_,records.empty());
}
void StatsPage::confirm_delete(Id id) {
    const auto records=app_.store().sessions_on(selected_day_);
    const auto record=std::find_if(records.begin(),records.end(),[id](const SessionRecord& record){return record.id==id;});
    if (record==records.end()) { refresh_records(); return; }
    std::string body;
    if (id==app_.active_session()) body=std::string(_("This also resets the current timer."))+"\n";
    if (record->total!=record->day_total) body+=std::string(_("Time on other dates will also be deleted."))+"\n";
    body+=_("This cannot be undone.");
    auto* dialog=adw_alert_dialog_new(_("Delete Focus Record?"),body.c_str());
    gtk_widget_add_css_class(GTK_WIDGET(dialog),"delete-record-dialog");
    auto* summary=ui::box(GTK_ORIENTATION_VERTICAL,10);
    ui::margins(summary,12);
    auto* task=ui::label(record->task,"delete-record-task");
    gtk_label_set_xalign(GTK_LABEL(task),0.5);
    gtk_label_set_justify(GTK_LABEL(task),GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(task),28);
    gtk_box_append(GTK_BOX(summary),task);
    auto* duration=ui::label(clock_text(record->total),"delete-record-duration");
    gtk_label_set_xalign(GTK_LABEL(duration),0.5);
    gtk_box_append(GTK_BOX(summary),duration);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog),summary);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),"cancel",_("Cancel"),"delete",_("Delete"),nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog),"delete",ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog),"cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog),"cancel");
    ui::response(dialog,[this,id](const std::string& response) {
        if (response=="delete") app_.delete_session(id);
    });
    adw_dialog_present(dialog,app_.window());
}
}
