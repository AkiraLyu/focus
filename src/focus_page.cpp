#include "focus_page.hpp"
#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace focus {
namespace {
GtkListItemFactory* task_factory(int max_chars) {
    auto* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory,"setup",G_CALLBACK(+[](GtkSignalListItemFactory*,GtkListItem* item,gpointer width) {
        auto* label = gtk_label_new(nullptr);
        gtk_label_set_xalign(GTK_LABEL(label),0);
        gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label),GPOINTER_TO_INT(width));
        gtk_list_item_set_child(item,label);
    }),GINT_TO_POINTER(max_chars));
    g_signal_connect(factory,"bind",G_CALLBACK(+[](GtkSignalListItemFactory*,GtkListItem* item,gpointer) {
        const auto* title = gtk_string_object_get_string(GTK_STRING_OBJECT(gtk_list_item_get_item(item)));
        auto* label = gtk_list_item_get_child(item);
        gtk_label_set_text(GTK_LABEL(label),title);
        gtk_widget_set_tooltip_text(label,title);
    }),nullptr);
    return factory;
}
}
FocusPage::FocusPage(Application& app) : app_(app) {
    root_ = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(root_),432);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(root_),360);
    auto* content = ui::box(GTK_ORIENTATION_VERTICAL,24);
    gtk_widget_set_valign(content,GTK_ALIGN_CENTER);
    ui::margins(content,24);
    adw_clamp_set_child(ADW_CLAMP(root_),content);
    auto* selectors = ui::box(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_box_set_homogeneous(GTK_BOX(selectors),true);
    gtk_box_append(GTK_BOX(content),selectors);
    const char* modes[] = {"番茄钟", "正向计时", nullptr};
    mode_ = gtk_drop_down_new_from_strings(modes);
    gtk_widget_set_tooltip_text(mode_,"计时方式");
    gtk_box_append(GTK_BOX(selectors),mode_);
    auto* overlay = gtk_overlay_new();
    gtk_widget_set_halign(overlay,GTK_ALIGN_CENTER);
    ring_ = gtk_drawing_area_new();
    gtk_widget_add_css_class(ring_,"accent");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ring_),300);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(ring_),300);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ring_),+[](GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer data) {
        static_cast<FocusPage*>(data)->draw_ring(cr,width,height);
    },this,nullptr);
    gtk_overlay_set_child(GTK_OVERLAY(overlay),ring_);
    auto* center = ui::box(GTK_ORIENTATION_VERTICAL,6);
    gtk_widget_set_halign(center,GTK_ALIGN_CENTER); gtk_widget_set_valign(center,GTK_ALIGN_CENTER);
    phase_ = ui::label("专注时间", "timer-phase");
    time_ = ui::label("25:00", "timer-digits");
    round_ = ui::label("", "dim-label");
    for (auto* widget : {phase_,time_,round_}) {
        gtk_label_set_wrap(GTK_LABEL(widget),false);
        gtk_widget_set_halign(widget,GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(center),widget);
    }
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay),center);
    gtk_box_append(GTK_BOX(content),overlay);
    picker_ = gtk_drop_down_new(nullptr,nullptr);
    auto* factory = task_factory(16);
    auto* list_factory = task_factory(30);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(picker_),factory);
    gtk_drop_down_set_list_factory(GTK_DROP_DOWN(picker_),list_factory);
    g_object_unref(factory);
    g_object_unref(list_factory);
    for (auto* selector : {mode_,picker_}) {
        gtk_widget_set_hexpand(selector,true);
        gtk_widget_set_size_request(selector,-1,40);
    }
    gtk_widget_set_tooltip_text(picker_,"当前任务");
    gtk_box_append(GTK_BOX(selectors),picker_);
    auto* buttons = ui::box(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_box_set_homogeneous(GTK_BOX(buttons),true);
    gtk_widget_set_size_request(buttons,-1,44);
    start_ = ui::button("开始专注","suggested-action");
    gtk_widget_add_css_class(start_,"pill");
    finish_ = ui::button("结束并保存"); gtk_widget_add_css_class(finish_,"pill");
    auto* reset = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(reset),"重置");
    gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(reset),true);
    gtk_widget_add_css_class(reset,"pill");
    auto* reset_menu = g_menu_new();
    g_menu_append(reset_menu,"重置当前计时","app.reset-current");
    g_menu_append(reset_menu,"重置整个番茄钟周期","app.reset-cycle");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(reset),G_MENU_MODEL(reset_menu));
    g_object_unref(reset_menu);
    gtk_box_append(GTK_BOX(buttons),start_); gtk_box_append(GTK_BOX(buttons),finish_);
    gtk_box_append(GTK_BOX(buttons),reset);
    gtk_box_append(GTK_BOX(content),buttons);
    ui::clicked(start_,[this]{app_.toggle_timer();});
    ui::clicked(finish_,[this]{app_.finish_timer();});
    ui::notify(G_OBJECT(mode_),"selected",root_,[this] {
        if (!loading_) app_.set_mode(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode_))==0 ? Mode::Pomodoro : Mode::Stopwatch);
    });
    ui::notify(G_OBJECT(picker_),"selected",root_,[this] {
        if (loading_) return;
        auto selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(picker_));
        if (selected < task_ids_.size()) app_.select_task(task_ids_[selected]);
    });
    reload_task_choices();
    update();
}
void FocusPage::draw_ring(cairo_t* cr, int width, int height) {
    GdkRGBA color;
    gtk_widget_get_color(ring_,&color);
    const double x = width/2.0, y = height/2.0, radius = std::min(width,height)/2.0-12;
    cairo_set_line_width(cr,3);
    cairo_set_source_rgba(cr,color.red,color.green,color.blue,0.12);
    cairo_arc(cr,x,y,radius,0,2*std::numbers::pi); cairo_stroke(cr);
    const auto& timer = app_.timer();
    const double progress = timer.mode()==Mode::Stopwatch ? std::fmod(static_cast<double>(timer.elapsed())/(60*second),1.0) :
        static_cast<double>(timer.display_time())/static_cast<double>(timer.duration());
    cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgba(cr,color.red,color.green,color.blue,1);
    cairo_arc(cr,x,y,radius,-std::numbers::pi/2,-std::numbers::pi/2+2*std::numbers::pi*std::max(0.005,progress));
    cairo_stroke(cr);
}
void FocusPage::update() {
    const auto& timer = app_.timer();
    const auto displayed = timer.mode()==Mode::Pomodoro ? timer.display_time()+second-1 : timer.display_time();
    gtk_label_set_text(GTK_LABEL(time_),clock_text(displayed).c_str());
    if (displayed>=3600*second) gtk_widget_add_css_class(time_,"long-time");
    else gtk_widget_remove_css_class(time_,"long-time");
    loading_ = true;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(mode_),timer.mode()==Mode::Pomodoro?0:1);
    loading_ = false;
    const char* phase = timer.mode()==Mode::Stopwatch ? "正向计时" : timer.phase()==Phase::Focus ? "专注时间" : timer.phase()==Phase::ShortBreak ? "短休息" : "长休息";
    gtk_label_set_text(GTK_LABEL(phase_),phase);
    const auto round = "已完成 " + std::to_string(timer.completed_rounds()) + " 个番茄";
    gtk_widget_set_visible(round_,timer.mode()==Mode::Pomodoro);
    gtk_label_set_text(GTK_LABEL(round_),round.c_str());
    gtk_button_set_label(GTK_BUTTON(start_),app_.timer_action_label());
    gtk_widget_set_sensitive(finish_,timer.engaged() || timer.phase()!=Phase::Focus);
    gtk_widget_set_sensitive(mode_,!timer.engaged());
    gtk_widget_set_sensitive(picker_,!timer.engaged());
    gtk_widget_queue_draw(ring_);
}
void FocusPage::reload_task_choices() {
    const auto tasks = app_.store().tasks();
    loading_ = true;
    auto* strings = gtk_string_list_new(nullptr);
    gtk_string_list_append(strings,"自由专注");
    task_ids_ = {0};
    guint selected = 0;
    for (const auto& task : tasks) {
        if (!task.done) {
            gtk_string_list_append(strings,task.title.c_str());
            task_ids_.push_back(task.id);
            if (task.id==app_.selected_task()) selected=static_cast<guint>(task_ids_.size()-1);
        }
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(picker_),G_LIST_MODEL(strings));
    g_object_unref(strings);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(picker_),selected);
    loading_ = false;
}
}
