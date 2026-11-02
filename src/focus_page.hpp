#pragma once
#include "application.hpp"
#include <vector>

namespace focus {
class FocusPage {
public:
    explicit FocusPage(Application& app);
    GtkWidget* widget() const { return root_; }
    void update();
    void reload_task_choices();
private:
    void draw_ring(cairo_t* cr, int width, int height);
    Application& app_;
    GtkWidget* root_;
    GtkWidget* mode_;
    GtkWidget* ring_;
    GtkWidget* time_;
    GtkWidget* phase_;
    GtkWidget* round_;
    GtkWidget* start_;
    GtkWidget* finish_;
    GtkWidget* picker_;
    std::vector<Id> task_ids_;
    bool loading_ = false;
};
}
