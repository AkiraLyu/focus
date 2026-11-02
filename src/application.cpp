#include "application.hpp"
#include "focus_page.hpp"
#include "tasks_page.hpp"
#include "notes_page.hpp"
#include "stats_page.hpp"
#include "tray.hpp"
#include "ui.hpp"
#include <algorithm>
#include <stdexcept>

namespace focus {
Application::Application(AdwApplication* app) : app_(app), store_(default_data_path()), timer_(store_.settings()) {
    window_ = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window_),"专注 · Focus");
    const auto state = store_.window_state();
    gtk_window_set_default_size(GTK_WINDOW(window_),state.width,state.height);
    gtk_widget_set_size_request(window_,720,600);
    if (state.maximized) gtk_window_maximize(GTK_WINDOW(window_));
    gtk_window_set_icon_name(GTK_WINDOW(window_),"io.github.focus.Timer");
    gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(gtk_widget_get_display(window_)),"/io/github/focus/Timer/icons");
    auto* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider,"/io/github/focus/Timer/style.css");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(window_),GTK_STYLE_PROVIDER(provider),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    auto* toolbar = adw_toolbar_view_new();
    auto* header = adw_header_bar_new();
    stack_ = ADW_VIEW_STACK(adw_view_stack_new());
    auto* switcher = adw_view_switcher_new();
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher),ADW_VIEW_SWITCHER_POLICY_WIDE);
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher),stack_);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header),switcher);
    settings_button_ = ui::icon_button("emblem-system-symbolic","计时设置");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header),settings_button_);
    auto* menu=gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu),"open-menu-symbolic");
    gtk_widget_set_tooltip_text(menu,"应用菜单");
    auto* model=g_menu_new();
    g_menu_append(model,"隐藏窗口","app.hide");
    g_menu_append(model,"退出","app.quit");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu),G_MENU_MODEL(model));
    g_object_unref(model);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header),menu);
    ui::clicked(settings_button_,[this]{show_settings();});
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar),header);
    toasts_ = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    adw_toast_overlay_set_child(toasts_,GTK_WIDGET(stack_));
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar),GTK_WIDGET(toasts_));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_),toolbar);
    focus_ = std::make_unique<FocusPage>(*this);
    tasks_ = std::make_unique<TasksPage>(*this);
    notes_ = std::make_unique<NotesPage>(*this);
    stats_ = std::make_unique<StatsPage>(*this);
    adw_view_stack_add_titled_with_icon(stack_,focus_->widget(),"focus","番茄钟","alarm-symbolic");
    adw_view_stack_add_titled_with_icon(stack_,tasks_->widget(),"tasks","长期任务","view-list-symbolic");
    adw_view_stack_add_titled_with_icon(stack_,notes_->widget(),"notes","Quick Notes","document-edit-symbolic");
    adw_view_stack_add_titled_with_icon(stack_,stats_->widget(),"stats","统计","view-statistics-symbolic");
    ui::notify(G_OBJECT(stack_),"visible-child-name",window_,[this] {
        notes_->flush();
        if (std::string(adw_view_stack_get_visible_child_name(stack_)) == "stats") stats_->refresh();
        if (std::string(adw_view_stack_get_visible_child_name(stack_)) == "tasks") tasks_->refresh_totals();
    });
    g_signal_connect(window_,"close-request",G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
        auto& self=*static_cast<Application*>(data);
        ui::guard(self.window_,[&]{self.hide();});
        return true;
    }),this);
    const auto activate=+[](GSimpleAction* action,GVariant*,gpointer data) {
        auto& self=*static_cast<Application*>(data);
        try {
            const std::string name=g_action_get_name(G_ACTION(action));
            if (name=="show") self.present();
            else if (name=="hide") self.hide();
            else if (name=="toggle-timer") self.toggle_timer();
            else if (name=="quit") self.quit();
        } catch (const std::exception& error) {
            self.present();
            ui::error(self.window_,error.what());
        }
    };
    const GActionEntry actions[]{
        {"show",activate,nullptr,nullptr,nullptr,{}},
        {"hide",activate,nullptr,nullptr,nullptr,{}},
        {"toggle-timer",activate,nullptr,nullptr,nullptr,{}},
        {"quit",activate,nullptr,nullptr,nullptr,{}}
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app_),actions,G_N_ELEMENTS(actions),this);
    const char* quit_keys[]={"<Primary>q",nullptr};
    const char* hide_keys[]={"<Primary>w",nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app_),"app.quit",quit_keys);
    gtk_application_set_accels_for_action(GTK_APPLICATION(app_),"app.hide",hide_keys);
    try {
        tray_=std::make_unique<Tray>(G_APPLICATION(app_),[this](const char* action,const std::string& token) {
            if (g_str_equal(action,"show") && !token.empty()) gtk_window_set_startup_id(GTK_WINDOW(window_),token.c_str());
            g_action_group_activate_action(G_ACTION_GROUP(app_),action,nullptr);
        });
    }
    catch (const std::exception& error) { toast(std::string("系统托盘不可用：")+error.what()); }
    g_application_hold(G_APPLICATION(app_));
    held_=true;
    refresh();
    tick_id_ = g_timeout_add(1000,+[](gpointer data) -> gboolean {
        auto& self = *static_cast<Application*>(data);
        try {
            self.tick();
            if (++self.ticks_ % 10 == 0) {
                self.stats_->refresh();
                self.tasks_->refresh_totals();
            }
        } catch (const std::exception& e) {
            self.timer_.pause(timer_now());
            self.refresh();
            self.present();
            ui::error(self.window_,std::string("计时已暂停，记录仍保留在内存中。请恢复存储后重试。\n")+e.what());
        }
        return G_SOURCE_CONTINUE;
    },this);
}
Application::~Application() {
    if (tick_id_) g_source_remove(tick_id_);
    if (inhibit_cookie_) gtk_application_uninhibit(GTK_APPLICATION(app_),inhibit_cookie_);
    tray_.reset();
    for (const char* action : {"show","hide","toggle-timer","quit"}) g_action_map_remove_action(G_ACTION_MAP(app_),action);
    if (window_) gtk_window_destroy(GTK_WINDOW(window_));
    if (held_) g_application_release(G_APPLICATION(app_));
}
void Application::present() {
    gtk_window_unminimize(GTK_WINDOW(window_));
    gtk_window_present(GTK_WINDOW(window_));
}
void Application::toast(const std::string& text) { adw_toast_overlay_add_toast(toasts_,adw_toast_new(text.c_str())); }
void Application::select_task(Id id) {
    if (timer_.engaged()) throw std::logic_error("请先结束当前计时，再切换任务");
    selected_task_ = id;
}
void Application::flush_session() { if (session_) store_.save_session(*session_); }
void Application::tick(bool pause) {
    const auto now=timer_now();
    auto event = pause ? timer_.pause(now) : timer_.advance(now);
    if (session_ && (event.focus_us || event.completed)) {
        session_->add(event.focus_us,g_get_real_time()-event.finished_ago_us);
        session_->completed = event.completed;
        flush_session();
    }
    if (event.phase_changed) {
        session_.reset();
        const std::string message = event.completed ? "专注完成，休息一下吧。准备好后开始休息。" : "休息结束，可以开始下一次专注。";
        toast(message);
        auto* notification = g_notification_new(event.completed ? "专注完成" : "休息结束");
        g_notification_set_body(notification,message.c_str());
        g_notification_set_default_action(notification,"app.show");
        g_application_send_notification(G_APPLICATION(app_),"timer-phase",notification);
        g_object_unref(notification);
        tasks_->refresh_totals();
        stats_->refresh();
    }
    refresh();
}
void Application::refresh() {
    focus_->update();
    gtk_widget_set_sensitive(settings_button_,!timer_.engaged());
    update_inhibitor();
    if (tray_) {
        const char* state=timer_.running()?(timer_.phase()==Phase::Focus?"专注":"休息"):
            timer_.elapsed()>0?"已暂停":"待开始";
        tray_->update(std::string(state)+" · "+clock_text(timer_.display_time()),timer_action_label());
    }
}
const char* Application::timer_action_label() const {
    return timer_.running()?"暂停":timer_.elapsed()>0?"继续计时":timer_.phase()==Phase::Focus?"开始专注":"开始休息";
}
void Application::update_inhibitor() {
    if (inhibit_requested_==timer_.running()) return;
    inhibit_requested_=timer_.running();
    if (inhibit_requested_) {
        inhibit_cookie_=gtk_application_inhibit(GTK_APPLICATION(app_),nullptr,GTK_APPLICATION_INHIBIT_SUSPEND,"计时进行中");
        if (!inhibit_cookie_) toast("系统未允许抑制休眠，唤醒后将更新计时");
    } else if (inhibit_cookie_) {
        gtk_application_uninhibit(GTK_APPLICATION(app_),inhibit_cookie_);
        inhibit_cookie_=0;
    }
}
void Application::toggle_timer() {
    if (timer_.running()) { tick(true); tasks_->refresh_totals(); }
    else {
        flush_session();
        if (session_ && session_->completed) session_.reset();
        if (timer_.phase() == Phase::Focus && !session_) session_ = store_.begin_session(selected_task_,timer_.mode());
        timer_.start(timer_now());
        refresh();
    }
    stats_->refresh();
}
void Application::finish_timer() {
    tick(true);
    flush_session();
    session_.reset();
    timer_.reset();
    tasks_->refresh_totals();
    refresh();
    stats_->refresh();
    toast("专注时长已保存");
}
void Application::set_mode(Mode mode) { timer_.set_mode(mode); refresh(); }
void Application::tasks_changed() {
    const auto tasks = store_.tasks();
    if (!timer_.engaged() && std::none_of(tasks.begin(),tasks.end(),[this](const Task& task) {
        return task.id==selected_task_ && !task.done;
    })) selected_task_ = 0;
    focus_->reload_task_choices();
    tasks_->reload();
    stats_->refresh();
}
void Application::save_window_state() {
    WindowState state;
    // GTK keeps this size unchanged while maximized or fullscreen.
    gtk_window_get_default_size(GTK_WINDOW(window_),&state.width,&state.height);
    state.maximized=gtk_window_is_maximized(GTK_WINDOW(window_));
    store_.save_window_state(state);
}
void Application::hide() {
    notes_->flush();
    tick();
    flush_session();
    save_window_state();
    gtk_widget_set_visible(window_,false);
}
void Application::quit() {
    notes_->flush();
    tick(true);
    flush_session();
    save_window_state();
    if (tick_id_) { g_source_remove(tick_id_); tick_id_=0; }
    gtk_window_destroy(GTK_WINDOW(window_));
    window_=nullptr;
    if (held_) { g_application_release(G_APPLICATION(app_)); held_=false; }
    g_application_quit(G_APPLICATION(app_));
}
void Application::show_settings() {
    if (timer_.engaged()) { toast("结束当前计时后可修改周期"); return; }
    auto* dialog = adw_preferences_dialog_new();
    adw_dialog_set_title(dialog,"计时设置");
    adw_dialog_set_content_width(dialog,480);
    auto* page = adw_preferences_page_new();
    auto* group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group),"番茄钟周期");
    const auto settings = timer_.settings();
    auto spin = [&](const char* title, int value, int min, int max) {
        auto* row = adw_spin_row_new_with_range(min,max,1);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),title);
        adw_spin_row_set_value(ADW_SPIN_ROW(row),value);
        adw_spin_row_set_snap_to_ticks(ADW_SPIN_ROW(row),true);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(group),row);
        return ADW_SPIN_ROW(row);
    };
    auto* focus = spin("专注时长（分钟）",settings.focus_minutes,1,180);
    auto* short_break = spin("短休息（分钟）",settings.short_break_minutes,1,60);
    auto* long_break = spin("长休息（分钟）",settings.long_break_minutes,1,120);
    auto* rounds = spin("长休息间隔（番茄数）",settings.rounds,2,12);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(group));
    auto* actions = adw_preferences_group_new();
    auto* save = ui::button("保存设置","suggested-action");
    gtk_widget_set_halign(save,GTK_ALIGN_END);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions),save);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(actions));
    ui::clicked(save,[this,dialog,focus,short_break,long_break,rounds] {
        for (auto* row : {focus,short_break,long_break,rounds}) adw_spin_row_update(row);
        Settings value{static_cast<int>(adw_spin_row_get_value(focus)),static_cast<int>(adw_spin_row_get_value(short_break)),
            static_cast<int>(adw_spin_row_get_value(long_break)),static_cast<int>(adw_spin_row_get_value(rounds))};
        store_.save_settings(value);
        timer_.set_settings(value);
        refresh();
        adw_dialog_close(dialog);
        toast("计时设置已保存");
    });
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog),ADW_PREFERENCES_PAGE(page));
    adw_dialog_present(dialog,window_);
}
}
