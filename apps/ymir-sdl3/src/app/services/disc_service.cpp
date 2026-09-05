#include "disc_service.hpp"
#include "rom_service.hpp"

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>
#include <app/profile.hpp>
#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <ymir/media/disc.hpp>
#include <ymir/media/loader/loader.hpp>
#include <ymir/sys/saturn.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/ymir.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <fmt/format.h>
#include <fmt/std.h>
#include <fstream>
#include <imgui.h>
#include <mutex>

namespace app::services {

DiscService::DiscService(SharedContext &context, Settings &settings, ShowModalCallback showModal)
    : m_context(context)
    , m_settings(settings)
    , m_showModal(std::move(showModal)) {}

DiscService::~DiscService() {
    std::lock_guard<std::mutex> lock(m_threadListMutex);
    for (auto itr = m_asyncDiscLoadThreads.begin(); itr != m_asyncDiscLoadThreads.end();) {
        if (itr->first.joinable()) {
            itr->first.join();
        }
        itr = m_asyncDiscLoadThreads.erase(itr);
    }
}
void DiscService::OpenLoadDiscDialog() {
    FileDialogParams params{};
    params.dialogTitle = "Load Sega Saturn disc image";
    params.defaultPath = m_context.state.loadedDiscImagePath;
    params.filters = {
        {.name = "All supported formats (*.ccd, *.chd, *.cue, *.iso, *.mds)", .filters = "ccd;chd;cue;iso;mds"},
        {.name = "All files (*.*)", .filters = "*"}};
    params.userdata = this;
    params.callback = [](void *userdata, const char *const *filelist, int filter) {
        static_cast<DiscService *>(userdata)->ProcessOpenDiscImageFileDialogSelection(filelist, filter);
    };

    m_context.EnqueueEvent(events::gui::OpenFile(std::move(params)));
}

void DiscService::ProcessOpenDiscImageFileDialogSelection(const char *const *filelist, int filter) {
    if (filelist == nullptr) {
        devlog::error<grp::base>("Failed to open file dialog: {}", SDL_GetError());
    } else if (*filelist == nullptr) {
        devlog::info<grp::base>("File dialog cancelled");
    } else {
        // Only one file should be selected
        const char *file = *filelist;
        std::string fileStr = file;
        const std::u8string u8File{fileStr.begin(), fileStr.end()};
        m_context.EnqueueEvent(events::emu::LoadDisc(u8File));
    }
}

bool DiscService::LoadDiscImage(std::filesystem::path path, bool showErrorModal) {
    // If any discs are being loaded asyncronously, wait until they are done
    {
        std::lock_guard<std::mutex> lock(m_threadListMutex);
        for (auto itr = m_asyncDiscLoadThreads.begin(); itr != m_asyncDiscLoadThreads.end();) {
            if (itr->first.joinable()) {
                itr->first.join();
            }
            itr = m_asyncDiscLoadThreads.erase(itr);
        }
    }

    // Try to load disc image from specified path
    devlog::info<grp::base>("Loading disc image from {}", path);
    ymir::media::Disc disc{};

    auto showError = [this, path](std::string message) {
        m_showModal("Error", [this, path, message] {
            ImGui::TextUnformatted(fmt::format("Could not load {} as a game disc image.", path).c_str());
            ImGui::NewLine();
            ImGui::TextUnformatted(message.c_str());
#ifdef __linux__
            // Check if we're running inside Flatpak's sandbox and warn user about filesystem permissions
            if (getenv("FLATPAK_ID") != nullptr) {
                ImGui::Separator();
                ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.medium);
                ImGui::TextColored(m_context.colors.notice, "Flatpak restricts access to the filesystem by default.");
                ImGui::TextColored(m_context.colors.notice,
                                   "You must manually grant Ymir permission to access the directory.");
                ImGui::PopFont();
                ImGui::TextUnformatted(
                    "You can ignore this error if you already granted permission to read the files.\n"
                    "In this case, the image is probably invalid or unsupported by Ymir.");
                ImGui::NewLine();
                ImGui::TextUnformatted("Learn more about Flatpak's sandbox system:");
                ImGui::Bullet();
                ImGui::TextLinkOpenURL("Flatpak - Sandbox permissions",
                                       R"(https://docs.flatpak.org/en/latest/sandbox-permissions.html)");
                ImGui::Bullet();
                ImGui::TextLinkOpenURL("Flatseal - Filesystem permissions",
                                       R"(https://github.com/tchx84/Flatseal/blob/master/DOCUMENTATION.md#filesystem)");
                ImGui::Bullet();
                ImGui::TextLinkOpenURL(
                    "How to configure Ymir using Flatseal",
                    R"(https://github.com/ymir-emu/Ymir/blob/main/TROUBLESHOOTING.md#game-discs-dont-load-with-the-flatpak-release)");
            }
#endif
            ImGui::Separator();
        });
    };

    bool hasErrors = false;
    if (!ymir::media::LoadDisc(path, disc, m_settings.general.preloadDiscImagesToRAM,
                               [&](ymir::media::MessageType type, std::string message) {
                                   switch (type) {
                                   case ymir::media::MessageType::InvalidFormat:
                                       devlog::trace<grp::media>("{}", message);
                                       break;
                                   case ymir::media::MessageType::Debug:
                                       devlog::trace<grp::media>("{}", message);
                                       break;
                                   case ymir::media::MessageType::Error:
                                       devlog::error<grp::media>("{}", message);
                                       if (showErrorModal) {
                                           hasErrors = true;
                                           showError(message);
                                       }
                                       break;
                                   case ymir::media::MessageType::NotValid:
                                       devlog::error<grp::media>("{}", message);
                                       if (showErrorModal && !hasErrors) {
                                           showError(message);
                                       }
                                       break;
                                   default: break;
                                   }
                               })) {
        devlog::error<grp::base>("Failed to load disc image");
        return false;
    }
    devlog::info<grp::base>("Disc image loaded succesfully");

    UpdateSettingsAndContext(disc, path);

    return true;
}

void DiscService::LoadDiscImageAsync(std::filesystem::path &path, bool showErrorModal) {
    // If a disc is being loaded asyncronously, wait until it is done
    std::lock_guard<std::mutex> lock(m_threadListMutex);
    for (auto itr = m_asyncDiscLoadThreads.begin(); itr != m_asyncDiscLoadThreads.end();) {
        if (itr->second->load()) {
            if (itr->first.joinable()) {
                itr->first.join();
            }
            itr = m_asyncDiscLoadThreads.erase(itr);
        } else {
            itr++;
        }
    }
    auto flag = std::make_shared<std::atomic<bool>>(false);
    m_asyncDiscLoadThreads.emplace_back(std::thread(&DiscService::AsyncDiscLoad, this, path, showErrorModal, flag), flag);
}

void DiscService::AsyncDiscLoad(std::filesystem::path path, bool showErrorModal,
                                std::shared_ptr<std::atomic<bool>> finishedFlag) {
    // Try to load disc image from specified path
    devlog::info<grp::base>("Loading disc image from {}", path);
    app::services::DiscService::AsyncLoadState loadState;
    loadState.disc = ymir::media::Disc{};
    ymir::media::Disc &disc = loadState.disc.value();
    loadState.path = path;

    auto showError = [this, path](std::string message) {
        m_showModal("Error", [this, path, message] {
            ImGui::TextUnformatted(fmt::format("Could not load {} as a game disc image.", path).c_str());
            ImGui::NewLine();
            ImGui::TextUnformatted(message.c_str());
#ifdef __linux__
            // Check if we're running inside Flatpak's sandbox and warn user about filesystem permissions
            if (getenv("FLATPAK_ID") != nullptr) {
                ImGui::Separator();
                ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.medium);
                ImGui::TextColored(m_context.colors.notice, "Flatpak restricts access to the filesystem by default.");
                ImGui::TextColored(m_context.colors.notice,
                                   "You must manually grant Ymir permission to access the directory.");
                ImGui::PopFont();
                ImGui::TextUnformatted(
                    "You can ignore this error if you already granted permission to read the files.\n"
                    "In this case, the image is probably invalid or unsupported by Ymir.");
                ImGui::NewLine();
                ImGui::TextUnformatted("Learn more about Flatpak's sandbox system:");
                ImGui::Bullet();
                ImGui::TextLinkOpenURL("Flatpak - Sandbox permissions",
                                       R"(https://docs.flatpak.org/en/latest/sandbox-permissions.html)");
                ImGui::Bullet();
                ImGui::TextLinkOpenURL("Flatseal - Filesystem permissions",
                                       R"(https://github.com/tchx84/Flatseal/blob/master/DOCUMENTATION.md#filesystem)");
                ImGui::Bullet();
                ImGui::TextLinkOpenURL(
                    "How to configure Ymir using Flatseal",
                    R"(https://github.com/StrikerX3/Ymir/blob/main/TROUBLESHOOTING.md#game-discs-dont-load-with-the-flatpak-release)");
            }
#endif
            ImGui::Separator();
        });
    };

    bool hasErrors = false;
    if (!ymir::media::LoadDisc(path, disc, m_settings.general.preloadDiscImagesToRAM,
                               [&](ymir::media::MessageType type, std::string message) {
                                   switch (type) {
                                   case ymir::media::MessageType::InvalidFormat:
                                       devlog::trace<grp::media>("{}", message);
                                       break;
                                   case ymir::media::MessageType::Debug:
                                       devlog::trace<grp::media>("{}", message);
                                       break;
                                   case ymir::media::MessageType::Error:
                                       devlog::error<grp::media>("{}", message);
                                       if (showErrorModal) {
                                           hasErrors = true;
                                           showError(message);
                                       }
                                       break;
                                   case ymir::media::MessageType::NotValid:
                                       devlog::error<grp::media>("{}", message);
                                       if (showErrorModal && !hasErrors) {
                                           showError(message);
                                       }
                                       break;
                                   default: break;
                                   }
                               })) {
        devlog::error<grp::base>("Failed to load disc image");
        loadState.disc = std::nullopt;
    } else {
        devlog::info<grp::base>("Disc image loaded succesfully");
    }
    m_context.EnqueueEvent(app::EmuEvent{app::EmuEvent::Type::ApplyDisc, std::move(loadState)});
    *finishedFlag = true;
}

void DiscService::LoadRecentDiscs() {
    auto listPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "recent_discs.txt";
    std::ifstream in{listPath};
    if (!in) {
        return;
    }

    m_context.state.recentDiscs.clear();
    while (in) {
        std::string line;
        if (!std::getline(in, line)) {
            break;
        }
        std::u8string u8line{line.begin(), line.end()};
        std::filesystem::path path = u8line;
        if (!path.empty()) {
            m_context.state.recentDiscs.push_back(path);
        }
    }
}

void DiscService::SaveRecentDiscs() {
    auto listPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "recent_discs.txt";
    std::ofstream out{listPath};
    for (auto &path : m_context.state.recentDiscs) {
        std::u8string u8path = path.u8string();
        out << reinterpret_cast<const char *>(u8path.data()) << "\n";
    }
}

void DiscService::UpdateSettingsAndContext(ymir::media::Disc &disc, std::filesystem::path &path) {
    // Insert disc into the Saturn drive
    {
        std::unique_lock lock{m_context.locks.disc};
        m_context.saturn.instance->LoadDisc(std::move(disc));
        if (m_context.saturn.GetConfiguration().system.autodetectRegion) {
            m_settings.system.videoStandard = m_context.saturn.GetConfiguration().system.videoStandard.Get();
            m_settings.MakeDirty();
        }
    }

    // Load new internal backup memory image if using per-game images
    if (m_settings.system.internalBackupRAMPerGame) {
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
    }

    // Update currently loaded disc path
    m_context.state.loadedDiscImagePath = path;
    m_context.state.loadedDiscDrivePath.clear();

    // Add to recent games list
    if (auto it = std::find(m_context.state.recentDiscs.begin(), m_context.state.recentDiscs.end(), path);
        it != m_context.state.recentDiscs.end()) {
        m_context.state.recentDiscs.erase(it);
    }
    m_context.state.recentDiscs.push_front(path);

    // Limit to 10 entries
    if (m_context.state.recentDiscs.size() > 10) {
        m_context.state.recentDiscs.resize(10);
    }

    SaveRecentDiscs();

    // Load cartridge
    if (m_settings.cartridge.autoLoadGameCarts) {
        if (auto *romService = m_context.serviceLocator.Get<ROMService>()) {
            romService->LoadRecommendedCartridge();
        }
    } else {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
    }

    m_context.rewindBuffer.Reset();

    if (m_context.paused && m_settings.general.unpauseOnDiscLoad) {
        m_context.EnqueueEvent(events::emu::SetPaused(false));
    }
}
} // namespace app::services
