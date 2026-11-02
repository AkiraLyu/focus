#pragma once
#include "store.hpp"
#include <adwaita.h>
#include <memory>
#include <optional>

namespace focus {
class FocusPage;
class TasksPage;
class NotesPage;
class StatsPage;
class Application {
public:
    explicit Application(AdwApplication* app);
    ~Application();
    void present();
    Store& store() { return store_; }
    const Timer& timer() const { return timer_; }
    GtkWidget* window() const { return window_; }
    Id selected_task() const { return selected_task_; }
    void select_task(Id id);
    void toggle_timer();
    void finish_timer();
    void set_mode(Mode mode);
    void tasks_changed();
    void show_settings();
    void toast(const std::string& message);
private:
    void tick(bool pause = false);
    void flush_session();
    void refresh();
    bool close();
    AdwApplication* app_;
    Store store_;
    Timer timer_;
    std::optional<Session> session_;
    Id selected_task_ = 0;
    GtkWidget* window_ = nullptr;
    AdwViewStack* stack_ = nullptr;
    AdwToastOverlay* toasts_ = nullptr;
    GtkWidget* settings_button_ = nullptr;
    guint tick_id_ = 0;
    unsigned ticks_ = 0;
    std::unique_ptr<FocusPage> focus_;
    std::unique_ptr<TasksPage> tasks_;
    std::unique_ptr<NotesPage> notes_;
    std::unique_ptr<StatsPage> stats_;
};
}
