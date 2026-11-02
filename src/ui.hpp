#pragma once
#include <adwaita.h>
#include <functional>
#include <string>

namespace focus::ui {
GtkWidget* box(GtkOrientation orientation, int spacing = 0);
GtkWidget* label(const std::string& text, const char* css = nullptr);
GtkWidget* button(const char* text, const char* css = nullptr);
GtkWidget* icon_button(const char* icon, const char* tooltip);
GtkWidget* empty_state(const char* icon, const char* title);
GtkWidget* scrolled(GtkWidget* child);
GtkWidget* page(GtkWidget* content);
void margins(GtkWidget* widget, int margin);
void clear_box(GtkWidget* box);
void clear_list(GtkWidget* list);
void on(GObject* object, const char* signal, GtkWidget* owner, std::function<void()> action);
void clicked(GtkWidget* widget, std::function<void()> action);
void notify(GObject* object, const char* property, GtkWidget* owner, std::function<void()> action);
void response(AdwDialog* dialog, std::function<void(const std::string&)> action);
void guard(GtkWidget* owner, const std::function<void()>& action);
void error(GtkWidget* owner, const std::string& message);
std::string text(GtkTextBuffer* buffer);
std::string trimmed(const char* value);
}
