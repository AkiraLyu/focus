#include "i18n.hpp"
#include "i18n_config.hpp"
#include <clocale>
#include <cstdarg>
#include <filesystem>
#include <libintl.h>

namespace focus::i18n {
void initialize() {
    std::setlocale(LC_ALL,"");
    std::filesystem::path directory=FOCUS_LOCALE_DIR;
    std::error_code error;
    const auto executable=std::filesystem::read_symlink("/proc/self/exe",error);
    if (!error) {
        // Support both the build tree and installations with a relocated prefix.
        for (const auto& candidate : {executable.parent_path()/"locale",
                executable.parent_path().parent_path()/FOCUS_LOCALE_SUBDIR}) {
            if (std::filesystem::is_directory(candidate,error)) { directory=candidate; break; }
        }
    }
    bindtextdomain(GETTEXT_PACKAGE,directory.c_str());
    bind_textdomain_codeset(GETTEXT_PACKAGE,"UTF-8");
    textdomain(GETTEXT_PACKAGE);
}
std::string format(const char* message, ...) {
    va_list arguments;
    va_start(arguments,message);
    auto* text=g_strdup_vprintf(message,arguments);
    va_end(arguments);
    std::string result(text);
    g_free(text);
    return result;
}
}
