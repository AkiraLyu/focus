#include "event_sound.hpp"
#include <canberra.h>
#include <gtk/gtk.h>

namespace focus {
EventSound::~EventSound() { if (context_) ca_context_destroy(context_); }
void EventSound::play_completion(const char* description) {
    auto* settings = gtk_settings_get_default();
    if (!settings) return;
    gboolean enabled = true;
    gchar* theme = nullptr;
    g_object_get(settings,"gtk-enable-event-sounds",&enabled,"gtk-sound-theme-name",&theme,nullptr);
    if (!enabled) { g_free(theme); return; }

    int result = CA_SUCCESS;
    if (!context_) {
        result = ca_context_create(&context_);
        if (result == CA_SUCCESS) result = ca_context_change_props(context_,
            CA_PROP_APPLICATION_ID,"io.github.focus.Timer",
            CA_PROP_APPLICATION_NAME,"专注",
            CA_PROP_APPLICATION_ICON_NAME,"io.github.focus.Timer",
            CA_PROP_MEDIA_ROLE,"event",nullptr);
    }
    if (result == CA_SUCCESS) result = ca_context_play(context_,0,
        CA_PROP_EVENT_ID,"alarm-clock-elapsed",
        CA_PROP_EVENT_DESCRIPTION,description,
        CA_PROP_CANBERRA_XDG_THEME_NAME,theme && *theme ? theme : "freedesktop",nullptr);
    g_free(theme);
    if (result < CA_SUCCESS) {
        if (result != CA_ERROR_DISABLED) g_message("系统提示音播放失败：%s",ca_strerror(result));
        if (context_) { ca_context_destroy(context_); context_ = nullptr; }
    }
}
}
