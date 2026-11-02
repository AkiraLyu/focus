#pragma once
#include "application.hpp"
#include <map>

namespace focus {
class TasksPage {
public:
    explicit TasksPage(Application& app);
    GtkWidget* widget() const { return root_; }
    void reload();
    void refresh_totals();
private:
    void edit_task(const Task* task = nullptr);
    Application& app_;
    GtkWidget* root_;
    GtkWidget* task_list_;
    GtkWidget* task_empty_;
    GtkWidget* show_done_;
    std::map<Id, AdwActionRow*> task_rows_;
};
}
