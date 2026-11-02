#pragma once
#include "application.hpp"
#include <array>

namespace focus {
class StatsPage {
public:
    explicit StatsPage(Application& app);
    GtkWidget* widget() const { return root_; }
    void show_overview();
    void refresh();
private:
    void show_day(const std::string& day);
    void refresh_records();
    void confirm_delete(Id id);
    struct DayWidgets {
        GtkWidget* button;
        GtkWidget* amount;
        GtkWidget* fill;
        GtkWidget* date;
        std::string day;
    };
    struct RecordWidgets {
        Id id;
        AdwActionRow* row;
        GtkWidget* duration;
        GtkWidget* day_duration;
    };
    Application& app_;
    GtkWidget* root_;
    GtkWidget* today_;
    GtkWidget* week_;
    GtkWidget* total_;
    GtkWidget* completed_;
    GtkWidget* tasks_;
    GtkWidget* empty_;
    std::array<DayWidgets,7> days_;
    AdwNavigationPage* details_;
    GtkWidget* records_;
    GtkWidget* records_scroll_;
    GtkWidget* records_empty_;
    GtkWidget* records_title_;
    GtkWidget* records_total_;
    std::string selected_day_;
    std::vector<RecordWidgets> record_rows_;
};
}
