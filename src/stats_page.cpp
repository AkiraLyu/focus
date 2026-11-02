#include "stats_page.hpp"
#include "ui.hpp"
#include <algorithm>

namespace focus {
StatsPage::StatsPage(Application& app) : app_(app) {
    auto* content=ui::box(GTK_ORIENTATION_VERTICAL,24);
    root_=ui::page(content);
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
    today_=metric("今日专注"); week_=metric("近七天"); total_=metric("累计专注"); completed_=metric("完成番茄");
    gtk_box_append(GTK_BOX(content),cards);
    auto* chart_card=ui::box(GTK_ORIENTATION_VERTICAL,18);
    gtk_widget_add_css_class(chart_card,"card"); gtk_widget_add_css_class(chart_card,"chart-card");
    gtk_box_append(GTK_BOX(chart_card),ui::label("近七天的专注", "title-3"));
    chart_=ui::box(GTK_ORIENTATION_HORIZONTAL,18); gtk_box_set_homogeneous(GTK_BOX(chart_),true);
    gtk_box_append(GTK_BOX(chart_card),chart_);
    gtk_box_append(GTK_BOX(content),chart_card);
    auto* task_box=ui::box(GTK_ORIENTATION_VERTICAL,12);
    gtk_box_append(GTK_BOX(task_box),ui::label("任务专注分布", "title-3"));
    tasks_=gtk_list_box_new(); gtk_list_box_set_selection_mode(GTK_LIST_BOX(tasks_),GTK_SELECTION_NONE);
    gtk_widget_add_css_class(tasks_,"boxed-list");
    gtk_box_append(GTK_BOX(task_box),tasks_);
    empty_=ui::empty_state("view-statistics-symbolic","暂无专注记录");
    gtk_box_append(GTK_BOX(task_box),empty_);
    gtk_box_append(GTK_BOX(content),task_box);
    refresh();
}
void StatsPage::refresh() {
    const auto stats=app_.store().statistics();
    auto metric=[](Microseconds value) {
        if (value<60*second) return std::to_string(value/second)+" 秒";
        if (value<3600*second) return std::to_string(value/(60*second))+" 分钟";
        char text[48]; g_snprintf(text,sizeof(text),"%.1f 小时",static_cast<double>(value)/(3600*second)); return std::string(text);
    };
    gtk_label_set_text(GTK_LABEL(today_),metric(stats.today).c_str());
    gtk_label_set_text(GTK_LABEL(week_),metric(stats.week).c_str());
    gtk_label_set_text(GTK_LABEL(total_),metric(stats.all).c_str());
    gtk_label_set_text(GTK_LABEL(completed_),std::to_string(stats.completed).c_str());
    ui::clear_box(chart_);
    Microseconds maximum=60*second;
    for (const auto& day : stats.days) maximum=std::max(maximum,day.total);
    for (size_t index=0;index<stats.days.size();++index) {
        const auto& day=stats.days[index];
        auto* column=ui::box(GTK_ORIENTATION_VERTICAL,10);
        auto* amount=ui::label(metric(day.total),"chart-value"); gtk_widget_set_halign(amount,GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(column),amount);
        auto* track=gtk_overlay_new(); gtk_widget_add_css_class(track,"bar-track");
        gtk_widget_set_size_request(track,-1,150);
        auto* fill=ui::box(GTK_ORIENTATION_VERTICAL);
        gtk_widget_add_css_class(fill,"bar-fill");
        gtk_widget_set_valign(fill,GTK_ALIGN_END);
        gtk_widget_set_size_request(fill,-1,day.total>0?std::max(3,static_cast<int>(150.0*static_cast<double>(day.total)/static_cast<double>(maximum))):0);
        gtk_widget_set_visible(fill,day.total>0);
        gtk_overlay_set_child(GTK_OVERLAY(track),fill);
        gtk_widget_set_tooltip_text(track,(day.date+" · "+duration_text(day.total)).c_str());
        gtk_box_append(GTK_BOX(column),track);
        auto* date=ui::label(index==6?"今天":day.date.substr(5),index==6?"accent":"dim-label");
        gtk_widget_set_halign(date,GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(column),date);
        gtk_box_append(GTK_BOX(chart_),column);
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
}
}
