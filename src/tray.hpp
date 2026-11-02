#pragma once
#include <gio/gio.h>
#include <libdbusmenu-glib/server.h>
#include <string>

namespace focus {
class Tray {
public:
    explicit Tray(GApplication* app);
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;
    void update(const std::string& status, const char* timer_action);
private:
    DbusmenuMenuitem* add_item(const char* label, const char* action);
    GVariant* property(const char* name) const;
    void cleanup();
    GApplication* app_;
    GDBusConnection* connection_ = nullptr;
    GDBusNodeInfo* interface_ = nullptr;
    GCancellable* cancellable_ = nullptr;
    DbusmenuServer* server_ = nullptr;
    DbusmenuMenuitem* root_ = nullptr;
    DbusmenuMenuitem* status_item_ = nullptr;
    DbusmenuMenuitem* timer_item_ = nullptr;
    GVariant* pixels_ = nullptr;
    guint registration_ = 0;
    guint watcher_ = 0;
    std::string status_ = "专注";
};
}
