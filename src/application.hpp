#pragma once
#include "focus_timer.hpp"
#include "event_sound.hpp"
#include <adwaita.h>
#include <memory>

namespace focus {
class FocusPage;
class TasksPage;
class NotesPage;
class StatsPage;
class Tray;
class Application {
public:
    explicit Application(AdwApplication* app);
    ~Application();
    void present();
    void quit();
    Store& store() { return store_; }
    const Timer& timer() const { return focus_timer_.timer(); }
    GtkWidget* window() const { return window_; }
    Id selected_task() const { return selected_task_; }
    void select_task(Id id);
    void toggle_timer();
    void finish_timer();
    void reset_timer(ResetScope scope);
    Id active_session() const { return focus_timer_.active_session(); }
    void delete_session(Id id);
    void set_mode(Mode mode);
    void tasks_changed();
    void show_settings();
    void toast(const std::string& message);
    const char* timer_action_label() const;
private:
    void tick(bool pause = false);
    void refresh();
    void refresh_visible_statistics();
    void hide();
    void save_window_state();
    void update_inhibitor();
    AdwApplication* app_;
    Store store_;
    FocusTimer focus_timer_;
    EventSound event_sound_;
    Id selected_task_ = 0;
    GtkWidget* window_ = nullptr;
    AdwViewStack* stack_ = nullptr;
    AdwToastOverlay* toasts_ = nullptr;
    GtkWidget* settings_button_ = nullptr;
    guint tick_id_ = 0;
    guint inhibit_cookie_ = 0;
    bool inhibit_requested_ = false;
    bool held_ = false;
    unsigned ticks_ = 0;
    std::unique_ptr<FocusPage> focus_;
    std::unique_ptr<TasksPage> tasks_;
    std::unique_ptr<NotesPage> notes_;
    std::unique_ptr<StatsPage> stats_;
    std::unique_ptr<Tray> tray_;
};
}
