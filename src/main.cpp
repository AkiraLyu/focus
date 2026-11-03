#include "application.hpp"
#include "i18n.hpp"
#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    focus::i18n::initialize();
    g_set_application_name(_("Focus"));
    auto* app = adw_application_new("io.github.focus.Timer", G_APPLICATION_DEFAULT_FLAGS);
    std::unique_ptr<focus::Application> controller;
    bool failed = false;
    struct Context { AdwApplication* app; std::unique_ptr<focus::Application>* controller; bool* failed; } context{app,&controller,&failed};
    g_signal_connect(app,"activate",G_CALLBACK(+[](GApplication*, gpointer data) {
        auto& c = *static_cast<Context*>(data);
        try {
            if (!*c.controller) *c.controller = std::make_unique<focus::Application>(c.app);
            (*c.controller)->present();
        } catch (const std::exception& e) {
            std::fprintf(stderr,_("Could not start Focus: %s\n"),e.what());
            *c.failed = true;
            g_application_quit(G_APPLICATION(c.app));
        }
    }),&context);
    const int status = g_application_run(G_APPLICATION(app),argc,argv);
    controller.reset();
    g_object_unref(app);
    return failed ? 1 : status;
}
