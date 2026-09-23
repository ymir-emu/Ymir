#pragma once

#include <filesystem>

namespace app {

struct CommandLineOptions {
    std::filesystem::path gameDiscPath;
    std::filesystem::path profilePath;
    bool forceUserProfile;
    bool fullScreen;
    bool startPaused;
    bool startFastForward;
    bool enableDebugTracing;
    bool autoConnectLocalLink;
};

} // namespace app
