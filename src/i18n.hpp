#pragma once
#include <glib/gi18n-lib.h>
#include <string>

#define P_(singular, plural, count) g_dngettext(GETTEXT_PACKAGE, singular, plural, count)

namespace focus::i18n {
// Call before creating application objects or starting worker threads.
void initialize();
std::string format(const char* message, ...) G_GNUC_PRINTF(1,2);
}
