#pragma once

#include <app/settings.hpp>

#include <ymir/media/disc.hpp>

#include <filesystem>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <thread>

// Forward declaration to break the circular depencencies
namespace app {
struct SharedContext;
}

namespace app::services {

/// @brief Handles loading disc images and the recent games list.
class DiscService {
public:
    struct AsyncLoadState {
        std::filesystem::path path;
        std::optional<ymir::media::Disc> disc;
    };

    using ShowModalCallback = std::function<void(std::string title, std::function<void()> contents)>;

    DiscService(SharedContext &context, Settings &settings, ShowModalCallback showModal);
    ~DiscService();

    DiscService(const DiscService &) = delete;
    DiscService &operator=(const DiscService &) = delete;

    /// @brief Opens the dialog to select a Saturn disc image.
    void OpenLoadDiscDialog();

    /// @brief Callback for the disc image file dialog selection.
    /// @param[in] filelist List of selected files.
    /// @param[in] filter Selected file dialog filter index.
    void ProcessOpenDiscImageFileDialogSelection(const char *const *filelist, int filter);

    /// @brief Loads a disc image file and updates the recent list.
    /// @param[in] path Path to the disc image.
    /// @param[in] showErrorModal Whether to show an error dialog if loading fails.
    /// @return True if successful.
    bool LoadDiscImage(std::filesystem::path path, bool showErrorModal);

    /// @brief Asynchronously calls AsyncDiscLoad
    /// @param[in] path A reference to the path of to the disc image.
    /// @param[in] showErrorModal Whether to show an error dialog if loading fails.
    void LoadDiscImageAsync(std::filesystem::path &path, bool showErrorModal);

    /// @brief Loads the list of recent discs from disk.
    void LoadRecentDiscs();

    /// @brief Saves the list of recent discs to disk.
    void SaveRecentDiscs();

    void UpdateSettingsAndContext(ymir::media::Disc &disc, std::filesystem::path &path);

private:
    SharedContext &m_context;
    Settings &m_settings;
    ShowModalCallback m_showModal;
    std::mutex m_threadListMutex;
    std::list<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> m_asyncDiscLoadThreads;

    /// @brief Preprocesses disc image file and enqueues an ApplyDisc event to load the disc after it has been
    /// preprocessed
    /// @param[in] path Path to the disc image.
    /// @param[in] showErrorModal Whether to show an error dialog if loading fails.
    /// @param[in] finishedFlag A smart pointer to a bool serving as a flag to indicate whether the disc has finished
    /// loading
    void AsyncDiscLoad(std::filesystem::path path, bool showErrorModal,
                       std::shared_ptr<std::atomic<bool>> finishedFlag);
};

} // namespace app::services
