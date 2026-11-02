#include "application.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace focus;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void pump(int milliseconds=100) {
    const auto end=g_get_monotonic_time()+milliseconds*1000;
    do {
        while (g_main_context_iteration(nullptr,false)) {}
        g_usleep(1000);
    } while (g_get_monotonic_time()<end);
}
void wait_for(const std::function<bool()>& condition,const char* message) {
    const auto end=g_get_monotonic_time()+5*second;
    do { pump(); if (condition()) return; } while (g_get_monotonic_time()<end);
    throw std::runtime_error(message);
}
void exercise(AdwApplication* app) {
    Application controller(app);
    g_signal_connect(app,"activate",G_CALLBACK(+[](GApplication*,gpointer data) {
        static_cast<Application*>(data)->present();
    }),&controller);
    auto* window=GTK_WINDOW(controller.window());
    gtk_window_set_default_size(window,900,680);
    controller.present();
    wait_for([&]{return gtk_widget_get_mapped(GTK_WIDGET(window));},"Window was not presented");
    controller.set_mode(Mode::Stopwatch);
    controller.toggle_timer();
    wait_for([&]{return controller.store().statistics().all>=second;},"Timer did not start");
    gtk_window_close(window);
    pump();
    check(!gtk_widget_get_visible(GTK_WIDGET(window)),"Closing must hide the window");
    check(controller.timer().running(),"Closing must preserve the running timer");
    const auto before=controller.store().statistics().all;
    wait_for([&]{return controller.store().statistics().all>before;},"Hidden timer stopped recording focus");
    const auto size=controller.store().window_state();
    check(size.width==900 && size.height==680,"Hiding must persist the normal window size");

    g_application_activate(G_APPLICATION(app));
    pump();
    check(controller.window()==GTK_WIDGET(window) && gtk_widget_get_visible(GTK_WIDGET(window)),"Activation must restore the existing window");
    check(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app)))==1,"Activation created a duplicate window");
    g_action_group_activate_action(G_ACTION_GROUP(app),"hide",nullptr);
    g_action_group_activate_action(G_ACTION_GROUP(app),"toggle-timer",nullptr);
    const auto paused=controller.store().statistics().all;
    pump(1200);
    check(!controller.timer().running() && controller.store().statistics().all==paused,"Pausing while hidden must stop recording time");
    g_action_group_activate_action(G_ACTION_GROUP(app),"show",nullptr);
    pump();
    check(gtk_widget_get_visible(GTK_WIDGET(window)),"Show action did not restore the window");
    g_action_group_activate_action(G_ACTION_GROUP(app),"toggle-timer",nullptr);
    check(controller.timer().running(),"Timer action did not resume focus");
    check(!g_action_group_get_action_enabled(G_ACTION_GROUP(app),"reset-cycle"),"Stopwatch mode must disable cycle reset");
    g_action_group_activate_action(G_ACTION_GROUP(app),"reset-current",nullptr);
    pump(1200);
    check(!controller.timer().engaged() && controller.store().statistics().all==0,"Current reset must discard saved stopwatch time without later restoring it");
    controller.set_mode(Mode::Pomodoro);
    check(g_action_group_get_action_enabled(G_ACTION_GROUP(app),"reset-cycle"),"Pomodoro mode must enable cycle reset");
    controller.toggle_timer();
    wait_for([&]{return controller.store().statistics().all>=second;},"Pomodoro did not record focus");
    g_action_group_activate_action(G_ACTION_GROUP(app),"reset-cycle",nullptr);
    check(!controller.timer().engaged() && controller.timer().phase()==Phase::Focus && controller.store().statistics().all==0,
          "Cycle reset must discard persisted focus and return to the first round");
    controller.quit();
    pump();
    check(!controller.timer().running(),"Quit must stop the timer");
    check(!gtk_application_get_windows(GTK_APPLICATION(app)),"Quit must destroy the window");
    g_signal_handlers_disconnect_by_data(app,&controller);
}
}
int main() {
    auto* temporary=g_dir_make_tmp("focus-background-test-XXXXXX",nullptr);
    if (!temporary) return 1;
    const std::filesystem::path path(temporary);
    g_setenv("XDG_DATA_HOME",temporary,true);
    g_free(temporary);
    if (!gtk_init_check()) { std::filesystem::remove_all(path); return 77; }
    adw_init();
    auto* app=adw_application_new("io.github.focus.Timer.BackgroundTest",G_APPLICATION_NON_UNIQUE);
    if (!g_application_register(G_APPLICATION(app),nullptr,nullptr)) {
        g_object_unref(app); std::filesystem::remove_all(path); return 77;
    }
    int status=0;
    try { exercise(app); std::cout<<"Background lifecycle checks passed.\n"; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; status=1; }
    g_object_unref(app);
    std::filesystem::remove_all(path);
    return status;
}
