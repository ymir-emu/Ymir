#include "i18n.hpp"

#include <SDL3/SDL.h>

#include <filesystem>

namespace app::ui::i18n {

void Initialize() {
    const char *basePath = SDL_GetBasePath();
    if (basePath == nullptr) {
        return;
    }

    const auto localePath = std::filesystem::path{basePath} / "locale";
    bindtextdomain("ymir-sdl3", localePath.string().c_str());
    bind_textdomain_codeset("ymir-sdl3", "UTF-8");
    textdomain("ymir-sdl3");
    SDL_free(const_cast<char *>(basePath));
}

} // namespace app::ui::i18n
