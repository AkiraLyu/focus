#pragma once
#include "application.hpp"

namespace focus {
class StatsPage {
public:
    explicit StatsPage(Application& app);
    GtkWidget* widget() const { return root_; }
    void refresh();
private:
    Application& app_;
    GtkWidget* root_;
    GtkWidget* today_;
    GtkWidget* week_;
    GtkWidget* total_;
    GtkWidget* completed_;
    GtkWidget* chart_;
    GtkWidget* tasks_;
    GtkWidget* empty_;
};
}
