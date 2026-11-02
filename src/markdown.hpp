#pragma once
#include <gtk/gtk.h>
#include <string>
namespace focus {
void render_markdown(GtkTextBuffer* buffer, const std::string& markdown);
}
