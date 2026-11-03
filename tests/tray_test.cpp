#include "tray.hpp"
#include "i18n.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
GVariant* call(GDBusConnection* bus,const char* path,const char* interface,const char* method,GVariant* parameters) {
    struct Reply { GMainLoop* loop; GVariant* value=nullptr; GError* error=nullptr; } reply{g_main_loop_new(nullptr,false)};
    g_dbus_connection_call(bus,g_dbus_connection_get_unique_name(bus),path,interface,method,parameters,nullptr,
        G_DBUS_CALL_FLAGS_NONE,5000,nullptr,+[](GObject* source,GAsyncResult* result,gpointer data) {
            auto& reply=*static_cast<Reply*>(data);
            reply.value=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&reply.error);
            g_main_loop_quit(reply.loop);
        },&reply);
    g_main_loop_run(reply.loop);
    g_main_loop_unref(reply.loop);
    if (reply.error) {
        const std::string message=reply.error->message;
        g_error_free(reply.error);
        throw std::runtime_error(message);
    }
    return reply.value;
}
void exercise(GApplication* app) {
    auto* bus=g_application_get_dbus_connection(app);
    std::string action,token;
    focus::Tray tray(app,[&](const char* name,const std::string& activation_token) {
        action=name; token=activation_token;
    });
    const auto item_call=[&](const char* method,GVariant* parameters) {
        g_variant_unref(call(bus,"/StatusNotifierItem","org.kde.StatusNotifierItem",method,parameters));
    };
    item_call("ProvideXdgActivationToken",g_variant_new("(s)","icon-gesture"));
    item_call("Activate",g_variant_new("(ii)",0,0));
    check(action=="show" && token=="icon-gesture","Tray clicks must forward the activation token when showing the window");
    item_call("Activate",g_variant_new("(ii)",0,0));
    check(action=="show" && token.empty(),"Activation tokens must not be reused");

    auto* reply=call(bus,"/StatusNotifierItem/Menu","com.canonical.dbusmenu","GetLayout",
        g_variant_new("(ii@as)",0,-1,g_variant_new_strv(nullptr,0)));
    auto* layout=g_variant_get_child_value(reply,1);
    auto* children=g_variant_get_child_value(layout,2);
    int show_id=-1;
    for (gsize i=0;i<g_variant_n_children(children);++i) {
        auto* child=g_variant_get_child_value(children,i);
        auto* row=g_variant_get_variant(child);
        auto* properties=g_variant_get_child_value(row,1);
        const char* label=nullptr;
        if (g_variant_lookup(properties,"label","&s",&label) && g_str_equal(label,"显示窗口"))
            g_variant_get_child(row,0,"i",&show_id);
        g_variant_unref(properties); g_variant_unref(row); g_variant_unref(child);
    }
    g_variant_unref(children); g_variant_unref(layout); g_variant_unref(reply);
    check(show_id>=0,"The tray menu must offer Show Window");
    item_call("ProvideXdgActivationToken",g_variant_new("(s)","menu-gesture"));
    g_variant_unref(call(bus,"/StatusNotifierItem/Menu","com.canonical.dbusmenu","Event",
        g_variant_new("(isvu)",show_id,"clicked",g_variant_new_int32(0),0u)));
    check(action=="show" && token=="menu-gesture","Show Window menu actions must forward the activation token");

    item_call("ProvideXdgActivationToken",g_variant_new("(s)","timer-gesture"));
    item_call("SecondaryActivate",g_variant_new("(ii)",0,0));
    check(action=="toggle-timer","Secondary activation must retain its timer action");
    item_call("Activate",g_variant_new("(ii)",0,0));
    check(token.empty(),"Timer actions must not leave a stale token for a later window activation");
}
}
int main() {
    focus::i18n::initialize();
    auto* app=g_application_new("io.github.focus.Timer.TrayTest",G_APPLICATION_NON_UNIQUE);
    if (!g_application_register(app,nullptr,nullptr) || !g_application_get_dbus_connection(app)) {
        g_object_unref(app); return 77;
    }
    int status=0;
    try { exercise(app); std::cout<<"Tray activation checks passed.\n"; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; status=1; }
    g_object_unref(app);
    return status;
}
