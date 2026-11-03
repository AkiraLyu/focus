#include "ui.hpp"
#include "i18n.hpp"
#include <exception>

namespace focus::ui {
namespace {
struct Action { GtkWidget* owner; std::function<void()> run; };
void destroy_action(gpointer data, GClosure*) { delete static_cast<Action*>(data); }
}
GtkWidget* box(GtkOrientation orientation, int spacing) { return gtk_box_new(orientation, spacing); }
GtkWidget* label(const std::string& text, const char* css) {
    auto* widget = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_label_set_wrap(GTK_LABEL(widget), true);
    if (css) gtk_widget_add_css_class(widget, css);
    return widget;
}
GtkWidget* button(const char* text, const char* css) {
    auto* widget = gtk_button_new_with_label(text);
    if (css) gtk_widget_add_css_class(widget, css);
    return widget;
}
GtkWidget* icon_button(const char* icon, const char* tooltip) {
    auto* widget = gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(widget, tooltip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL, tooltip, -1);
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
    return widget;
}
GtkWidget* empty_state(const char* icon, const char* title) {
    // The parent owns scrolling; this content reports its full required height.
    auto* content = box(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_add_css_class(content,"empty-state");
    auto* image = gtk_image_new_from_icon_name(icon);
    gtk_image_set_pixel_size(GTK_IMAGE(image),48);
    gtk_widget_add_css_class(image,"dim-label");
    gtk_widget_set_halign(image,GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content),image);
    auto* text = label(title,"title-3");
    gtk_label_set_xalign(GTK_LABEL(text),0.5);
    gtk_label_set_justify(GTK_LABEL(text),GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(content),text);
    return content;
}
GtkWidget* scrolled(GtkWidget* child) {
    auto* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), child);
    gtk_widget_set_vexpand(scroll, true);
    return scroll;
}
GtkWidget* page(GtkWidget* content) {
    gtk_widget_add_css_class(content, "page-content");
    auto* clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 940);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 700);
    adw_clamp_set_child(ADW_CLAMP(clamp), content);
    return scrolled(clamp);
}
void margins(GtkWidget* widget, int margin) {
    gtk_widget_set_margin_start(widget,margin); gtk_widget_set_margin_end(widget,margin);
    gtk_widget_set_margin_top(widget,margin); gtk_widget_set_margin_bottom(widget,margin);
}
void clear_box(GtkWidget* box) { while (auto* child=gtk_widget_get_first_child(box)) gtk_box_remove(GTK_BOX(box),child); }
void clear_list(GtkWidget* list) { while (auto* child=gtk_widget_get_first_child(list)) gtk_list_box_remove(GTK_LIST_BOX(list),child); }
void error(GtkWidget* owner, const std::string& message) {
    auto* dialog = adw_alert_dialog_new(_("Unable to Complete Action"),message.c_str());
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog),"close",_("OK"));
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog),"close");
    adw_dialog_present(dialog, owner);
}
void guard(GtkWidget* owner, const std::function<void()>& action) {
    try { action(); } catch (const std::exception& e) { error(owner,e.what()); }
}
void on(GObject* object, const char* signal, GtkWidget* owner, std::function<void()> action) {
    g_signal_connect_data(object,signal,G_CALLBACK(+[](gpointer, gpointer data) {
        auto& action = *static_cast<Action*>(data);
        // The action may rebuild and destroy the widget which owns this closure.
        auto run = action.run;
        guard(action.owner,run);
    }),new Action{owner,std::move(action)},destroy_action,G_CONNECT_DEFAULT);
}
void clicked(GtkWidget* widget, std::function<void()> action) { on(G_OBJECT(widget),"clicked",widget,std::move(action)); }
void notify(GObject* object, const char* property, GtkWidget* owner, std::function<void()> action) {
    const auto signal = std::string("notify::") + property;
    g_signal_connect_data(object,signal.c_str(),G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        auto& action = *static_cast<Action*>(data);
        auto run = action.run;
        guard(action.owner,run);
    }),new Action{owner,std::move(action)},destroy_action,G_CONNECT_DEFAULT);
}
void response(AdwDialog* dialog, std::function<void(const std::string&)> action) {
    using Response = std::function<void(const std::string&)>;
    g_signal_connect_data(dialog,"response",G_CALLBACK(+[](AdwAlertDialog* self, const char* id, gpointer data) {
        auto run = *static_cast<Response*>(data);
        guard(GTK_WIDGET(self),[&]{run(id);});
    }),new Response(std::move(action)),+[](gpointer data,GClosure*){delete static_cast<Response*>(data);},G_CONNECT_DEFAULT);
}
std::string text(GtkTextBuffer* buffer) {
    GtkTextIter start,end; gtk_text_buffer_get_bounds(buffer,&start,&end);
    gchar* value = gtk_text_buffer_get_text(buffer,&start,&end,false);
    std::string result(value); g_free(value); return result;
}
std::string trimmed(const char* value) {
    gchar* copy = g_strdup(value); g_strstrip(copy); std::string result(copy); g_free(copy); return result;
}
}
