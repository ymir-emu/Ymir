#pragma once

#include <libintl.h>

#include <fmt/format.h>

// Keep the gettext marker in the shared header so xgettext can discover every
// call site without a per-file wrapper macro.
#ifndef _
    #define _(text) gettext(text)
#endif

// Keep ImGui widget IDs out of the translatable source string.
#define TR_ID(text, id) (fmt::format("{}##{}", _(text), id).c_str())

namespace app::ui::i18n {

/// Binds the application's compiled gettext catalogs to the runtime locale path.
void Initialize();

} // namespace app::ui::i18n
