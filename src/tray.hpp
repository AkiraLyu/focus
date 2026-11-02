#pragma once
#include <gio/gio.h>
#include <libdbusmenu-glib/server.h>
#include <functional>
#include <string>

namespace focus {
class Tray {
public:
    using Activate = std::function<void(const char* action, const std::string& token)>;
    Tray(GApplication* app, Activate activate);
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;
    void update(const std::string& status, const char* timer_action);
private:
    void activate(const char* action);
    DbusmenuMenuitem* add_item(const char* label, const char* action);
    GVariant* property(const char* name) const;
    void cleanup();
    Activate activate_;
    std::string activation_token_;
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
