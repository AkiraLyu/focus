#include "tray.hpp"
#include "i18n.hpp"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace focus {
namespace {
constexpr auto item_path = "/StatusNotifierItem";
constexpr auto menu_path = "/StatusNotifierItem/Menu";
constexpr auto item_interface = "org.kde.StatusNotifierItem";
constexpr auto icon_name = "io.github.focus.Timer";
struct MenuAction { Tray* tray; std::string name; };
GVariant* icon_pixels() {
    GVariantBuilder builder;
    g_variant_builder_init(&builder,G_VARIANT_TYPE("a(iiay)"));
    auto* icon = gdk_pixbuf_new_from_resource_at_scale(
        "/io/github/focus/Timer/icons/scalable/apps/io.github.focus.Timer.svg",32,32,true,nullptr);
    if (icon) {
        const int width=gdk_pixbuf_get_width(icon),height=gdk_pixbuf_get_height(icon);
        const int stride=gdk_pixbuf_get_rowstride(icon),channels=gdk_pixbuf_get_n_channels(icon);
        const auto* source=gdk_pixbuf_read_pixels(icon);
        std::vector<guint8> bytes(static_cast<size_t>(width*height*4));
        for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
            const auto* pixel=source+y*stride+x*channels;
            auto* out=bytes.data()+(y*width+x)*4;
            // StatusNotifier pixmaps use network-order ARGB, not RGBA.
            out[0]=channels==4?pixel[3]:255;
            out[1]=pixel[0]; out[2]=pixel[1]; out[3]=pixel[2];
        }
        g_variant_builder_add(&builder,"(ii@ay)",width,height,
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,bytes.data(),bytes.size(),1));
        g_object_unref(icon);
    }
    return g_variant_ref_sink(g_variant_builder_end(&builder));
}
}
Tray::Tray(GApplication* app, Activate activate) : activate_(std::move(activate)), status_(_("Focus")) {
    try {
        auto* bus=g_application_get_dbus_connection(app);
        if (!bus) throw std::runtime_error(_("Cannot connect to the session bus for the system tray."));
        connection_=G_DBUS_CONNECTION(g_object_ref(bus));
        cancellable_=g_cancellable_new();
        pixels_=icon_pixels();
        auto* xml=g_resources_lookup_data("/io/github/focus/Timer/status-notifier.xml",G_RESOURCE_LOOKUP_FLAGS_NONE,nullptr);
        if (!xml) throw std::runtime_error(_("The system tray interface resource is missing."));
        const std::string description(static_cast<const char*>(g_bytes_get_data(xml,nullptr)),g_bytes_get_size(xml));
        g_bytes_unref(xml);
        interface_=g_dbus_node_info_new_for_xml(description.c_str(),nullptr);
        if (!interface_) throw std::runtime_error(_("The system tray interface is invalid."));
        static const GDBusInterfaceVTable handlers{
            +[](GDBusConnection*,const char*,const char*,const char*,const char* method,GVariant* parameters,GDBusMethodInvocation* invocation,gpointer data) {
                auto& self=*static_cast<Tray*>(data);
                if (g_str_equal(method,"ProvideXdgActivationToken")) {
                    const char* token=nullptr;
                    g_variant_get(parameters,"(&s)",&token);
                    self.activation_token_=token;
                } else if (g_str_equal(method,"Activate")) self.activate("show");
                else if (g_str_equal(method,"SecondaryActivate")) self.activate("toggle-timer");
                // Hosts render the exported menu on ContextMenu; scrolling has no action.
                g_dbus_method_invocation_return_value(invocation,nullptr);
            },
            +[](GDBusConnection*,const char*,const char*,const char*,const char* name,GError**,gpointer data) -> GVariant* {
                return static_cast<Tray*>(data)->property(name);
            },nullptr,{}
        };
        GError* error=nullptr;
        registration_=g_dbus_connection_register_object(connection_,item_path,interface_->interfaces[0],&handlers,this,nullptr,&error);
        if (!registration_) {
            std::string message=error->message; g_error_free(error);
            throw std::runtime_error(message);
        }
        server_=dbusmenu_server_new(menu_path);
        root_=dbusmenu_menuitem_new();
        status_item_=add_item(_("Focus"),nullptr);
        add_item(_("Show Window"),"show");
        timer_item_=add_item(_("Start Focus"),"toggle-timer");
        add_item(_("Quit"),"quit");
        dbusmenu_server_set_root(server_,root_);
        watcher_=g_bus_watch_name_on_connection(connection_,"org.kde.StatusNotifierWatcher",G_BUS_NAME_WATCHER_FLAGS_NONE,
            +[](GDBusConnection* connection,const char*,const char*,gpointer data) {
                auto& self=*static_cast<Tray*>(data);
                g_dbus_connection_call(connection,"org.kde.StatusNotifierWatcher","/StatusNotifierWatcher",
                    "org.kde.StatusNotifierWatcher","RegisterStatusNotifierItem",g_variant_new("(s)",item_path),nullptr,
                    G_DBUS_CALL_FLAGS_NONE,5000,self.cancellable_,+[](GObject* source,GAsyncResult* result,gpointer) {
                        GError* error=nullptr;
                        auto* reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);
                        if (reply) g_variant_unref(reply);
                        if (error) {
                            if (!g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) g_message(_("Tray registration failed: %s"),error->message);
                            g_error_free(error);
                        }
                    },nullptr);
            },nullptr,this,nullptr);
    } catch (...) { cleanup(); throw; }
}
Tray::~Tray() { cleanup(); }
void Tray::activate(const char* action) {
    // Activation tokens belong to one user gesture, including menu actions.
    const auto token=std::exchange(activation_token_,{});
    activate_(action,token);
}
void Tray::cleanup() {
    if (cancellable_) g_cancellable_cancel(cancellable_);
    if (watcher_) g_bus_unwatch_name(watcher_);
    if (registration_) g_dbus_connection_unregister_object(connection_,registration_);
    g_clear_object(&server_);
    g_clear_object(&root_);
    g_clear_pointer(&interface_,g_dbus_node_info_unref);
    g_clear_pointer(&pixels_,g_variant_unref);
    g_clear_object(&cancellable_);
    g_clear_object(&connection_);
}
DbusmenuMenuitem* Tray::add_item(const char* label,const char* action) {
    auto* item=dbusmenu_menuitem_new();
    dbusmenu_menuitem_property_set(item,DBUSMENU_MENUITEM_PROP_LABEL,label);
    dbusmenu_menuitem_property_set_bool(item,DBUSMENU_MENUITEM_PROP_VISIBLE,true);
    dbusmenu_menuitem_property_set_bool(item,DBUSMENU_MENUITEM_PROP_ENABLED,action!=nullptr);
    if (action) g_signal_connect_data(item,DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
        G_CALLBACK(+[](DbusmenuMenuitem*,guint,gpointer data) {
            const auto command=*static_cast<MenuAction*>(data);
            command.tray->activate(command.name.c_str());
        }),new MenuAction{this,action},+[](gpointer data,GClosure*){delete static_cast<MenuAction*>(data);},G_CONNECT_DEFAULT);
    dbusmenu_menuitem_child_append(root_,item);
    g_object_unref(item);
    return item; // Owned by the root menu.
}
GVariant* Tray::property(const char* name) const {
    const std::string_view key(name);
    if (key=="Category") return g_variant_new_string("ApplicationStatus");
    if (key=="Id" || key=="IconName") return g_variant_new_string(icon_name);
    if (key=="Title") return g_variant_new_string(_("Focus"));
    if (key=="Status") return g_variant_new_string("Active");
    if (key=="WindowId") return g_variant_new_uint32(0);
    if (key=="Menu") return g_variant_new_object_path(menu_path);
    if (key=="ItemIsMenu") return g_variant_new_boolean(false);
    if (key=="IconPixmap") return g_variant_ref(pixels_);
    if (key=="ToolTip") return g_variant_new("(s@a(iiay)ss)",icon_name,g_variant_ref(pixels_),_("Focus"),status_.c_str());
    if (key=="AttentionIconPixmap" || key=="OverlayIconPixmap") return g_variant_new_array(G_VARIANT_TYPE("(iiay)"),nullptr,0);
    return g_variant_new_string("");
}
void Tray::update(const std::string& status,const char* timer_action) {
    dbusmenu_menuitem_property_set(timer_item_,DBUSMENU_MENUITEM_PROP_LABEL,timer_action);
    if (status_==status) return;
    status_=status;
    dbusmenu_menuitem_property_set(status_item_,DBUSMENU_MENUITEM_PROP_LABEL,status_.c_str());
    g_dbus_connection_emit_signal(connection_,nullptr,item_path,item_interface,"NewToolTip",nullptr,nullptr);
}
}
