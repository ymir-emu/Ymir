#pragma once

#include <app/audio_system.hpp>
#include <app/display.hpp>
#include <app/message.hpp>
#include <app/profile.hpp>
#include <app/rewind_buffer.hpp>
#include <app/rom_manager.hpp>
#include <app/update_checker.hpp>

#include <app/services/gfx/gfx_gui_types.hpp>

#include <app/input/input_context.hpp>
#include <app/input/input_utils.hpp>

#include <app/debug/cd_drive_tracer.hpp>
#include <app/debug/cdblock_tracer.hpp>
#include <app/debug/scsp_tracer.hpp>
#include <app/debug/scu_tracer.hpp>
#include <app/debug/sh2_tracer.hpp>
#include <app/debug/ygr_tracer.hpp>

#include <app/events/emu_event.hpp>
#include <app/events/gui_event.hpp>

#include <util/service_locator.hpp>

#include <ymir/hw/smpc/peripheral/peripheral_state_common.hpp>

#include <ymir/core/configuration.hpp>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/event.hpp>

#include <imgui.h>

#include <SDL3/SDL_render.h>

#include <blockingconcurrentqueue.h>

#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>

// -----------------------------------------------------------------------------
// Forward declarations

namespace ymir {

struct Saturn;

namespace sys {
    struct SystemMemory;
} // namespace sys

namespace sh1 {
    class SH1;
} // namespace sh1

namespace sh2 {
    class SH2;
} // namespace sh2

namespace scu {
    class SCU;
} // namespace scu

namespace vdp {
    class VDP;
} // namespace vdp

namespace smpc {
    class SMPC;
} // namespace smpc

namespace scsp {
    class SCSP;
} // namespace scsp

namespace cdblock {
    class CDBlock;
} // namespace cdblock

namespace cart {
    class BaseCartridge;
} // namespace cart

namespace media {
    class CDInterface;
    struct SaturnHeader;
} // namespace media

} // namespace ymir

// -----------------------------------------------------------------------------
// Implementation

namespace app {

namespace grp {

    // -------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   media
    //   updater

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "App";
    };

    struct media : public base {
        static constexpr std::string_view name = "App-Media";
    };

    struct updater : public base {
        static constexpr std::string_view name = "App-Updater";
    };

} // namespace grp

struct SharedContext {
    util::ServiceLocator serviceLocator;

    struct SaturnContainer {
        std::unique_ptr<ymir::Saturn> instance;

        ymir::sys::SystemMemory &GetSystemMemory();
        ymir::sys::SH2Bus &GetMainBus();
        ymir::sh2::SH2 &GetMasterSH2();
        ymir::sh2::SH2 &GetSlaveSH2();
        ymir::sh2::SH2 &GetSH2(bool master) {
            return master ? GetMasterSH2() : GetSlaveSH2();
        }
        ymir::scu::SCU &GetSCU();
        ymir::vdp::VDP &GetVDP();
        ymir::smpc::SMPC &GetSMPC();
        ymir::scsp::SCSP &GetSCSP();
        ymir::cdblock::CDBlock &GetCDBlock();
        ymir::cart::BaseCartridge &GetCartridge();
        ymir::sh1::SH1 &GetSH1();

        const ymir::sys::SH2Bus &GetMainBus() const {
            return const_cast<SaturnContainer *>(this)->GetMainBus();
        }
        const ymir::sh2::SH2 &GetMasterSH2() const {
            return const_cast<SaturnContainer *>(this)->GetMasterSH2();
        }
        const ymir::sh2::SH2 &GetSlaveSH2() const {
            return const_cast<SaturnContainer *>(this)->GetSlaveSH2();
        }
        const ymir::sh2::SH2 &GetSH2(bool master) const {
            return const_cast<SaturnContainer *>(this)->GetSH2(master);
        }
        const ymir::scu::SCU &GetSCU() const {
            return const_cast<SaturnContainer *>(this)->GetSCU();
        }
        const ymir::vdp::VDP &GetVDP() const {
            return const_cast<SaturnContainer *>(this)->GetVDP();
        }
        const ymir::smpc::SMPC &GetSMPC() const {
            return const_cast<SaturnContainer *>(this)->GetSMPC();
        }
        const ymir::scsp::SCSP &GetSCSP() const {
            return const_cast<SaturnContainer *>(this)->GetSCSP();
        }
        const ymir::cdblock::CDBlock &GetCDBlock() const {
            return const_cast<SaturnContainer *>(this)->GetCDBlock();
        }

        bool IsSlaveSH2Enabled() const;
        void SetSlaveSH2Enabled(bool enabled);
        bool IsDebugTracingEnabled() const;
        bool IsSH2CacheEmulationEnabled() const;

        [[nodiscard]] ymir::XXH128Hash GetIPLHash() const;
        [[nodiscard]] ymir::XXH128Hash GetCDBlockROMHash() const;
        [[nodiscard]] ymir::XXH128Hash GetDiscHash() const;
        [[nodiscard]] const ymir::media::SaturnHeader &GetDiscHeader() const;
        [[nodiscard]] const ymir::media::CDInterface &GetCDInterface() const;

        ymir::core::Configuration &GetConfiguration();
        const ymir::core::Configuration &GetConfiguration() const {
            return const_cast<SaturnContainer *>(this)->GetConfiguration();
        }
    } saturn;

    AudioSystem audioSystem;
    std::chrono::steady_clock::time_point lastVolumeChangeTime;

    struct DisplayInfo {
        std::string name;
        SDL_Rect bounds;
        std::vector<display::DisplayMode> modes;
    };

    struct Display {
        std::unordered_map<SDL_DisplayID, DisplayInfo> list;

        SDL_DisplayID id = 0;

        // Find and use closest display match
        void UseDisplay(std::string_view name, int x, int y) {
            // Empty name means default display (ID = 0)
            if (name.empty()) {
                id = 0;
                return;
            }

            // Find best match from given parameters
            SDL_DisplayID bestMatch = 0;
            int bestMatchBoundsDist;
            for (auto &[id, display] : list) {
                if (name == display.name) {
                    if (x == display.bounds.x && y == display.bounds.y) {
                        // Found exact match
                        this->id = id;
                        return;
                    }

                    // Find closest match instead
                    const int dx = x - display.bounds.x;
                    const int dy = y - display.bounds.y;
                    const int d = dx * dx + dy * dy;
                    if (bestMatch == 0 || d < bestMatchBoundsDist) {
                        bestMatch = id;
                        bestMatchBoundsDist = d;
                    }
                }
            }
            id = bestMatch;
        }
    } display;

    struct Screen {
        Screen() {
            SetResolution(ymir::vdp::kDefaultResH, ymir::vdp::kDefaultResV);
            prevWidth = width;
            prevHeight = height;
            prevScaleX = scaleX;
            prevScaleY = scaleY;
        }

        SDL_Window *window = nullptr;

        uint32 width;
        uint32 height;
        uint32 scaleX;
        uint32 scaleY;
        uint32 fbScale = 1;

        double scale = 1.0;             // final computed display scale
        int dCenterX = 0, dCenterY = 0; // display position (center) on window
        int dSizeX = 1, dSizeY = 1;     // display size on window
        bool doubleResH = false;
        bool doubleResV = false;

        // Native VDP2 resolution
        uint32 nativeWidth = ymir::vdp::kDefaultResH;
        uint32 nativeHeight = ymir::vdp::kDefaultResV;

        // Rendered resolution (possibly scaled)
        uint32 renderWidth = ymir::vdp::kDefaultResH;
        uint32 renderHeight = ymir::vdp::kDefaultResV;

        // Client area dimensions
        uint32 clientWidth = ymir::vdp::kDefaultResH;
        uint32 clientHeight = ymir::vdp::kDefaultResV;

        // Output texture dimensions
        uint32 targetOutputWidth = ymir::vdp::kMaxResH;
        uint32 targetOutputHeight = ymir::vdp::kMaxResV;
        uint32 currOutputWidth = ymir::vdp::kMaxResH;
        uint32 currOutputHeight = ymir::vdp::kMaxResV;

        // Hacky garbage to help automatically resize window on resolution changes
        bool resolutionChanged = false;
        uint32 prevWidth;
        uint32 prevHeight;
        uint32 prevScaleX;
        uint32 prevScaleY;

        void SetResolution(uint32 newWidth, uint32 newHeight) {
            // TODO: remove/replace this
            if (width == newWidth && height == newHeight) {
                return;
            }

            doubleResH = newWidth > ymir::vdp::kMaxNormalResH;
            doubleResV = newHeight > ymir::vdp::kMaxNormalResV;

            prevWidth = width;
            prevHeight = height;
            prevScaleX = scaleX;
            prevScaleY = scaleY;

            width = newWidth;
            height = newHeight;
            scaleX = doubleResV && !doubleResH ? 2 : 1;
            scaleY = doubleResH && !doubleResV ? 2 : 1;
            resolutionChanged = true;
        }

        void SetNativeResolution(uint32 width, uint32 height) {
            nativeWidth = width;
            nativeHeight = height;
        }

        void SetRenderResolution(uint32 width, uint32 height) {
            renderWidth = width;
            renderHeight = height;
        }

        bool SetClientAreaSize(uint32 width, uint32 height) {
            const bool changed = clientWidth != width || clientHeight != height;
            clientWidth = width;
            clientHeight = height;
            return changed;
        }

        void SetOutputResolution(uint32 width, uint32 height) {
            targetOutputWidth = width;
            targetOutputHeight = height;
        }

        // Display framebuffers -- emu renders to one, GUI displays the other
        using Framebuffer = std::array<uint32, ymir::vdp::kMaxResH * ymir::vdp::kMaxResV>;
        std::array<Framebuffer, 2> framebuffers;
        size_t currBackFramebuffer = 0;
        std::mutex mtxFramebuffer;
        bool updated = false;

        void CopyFramebufferToTexture(void *data, size_t pitch) const {
            auto pixelData = static_cast<uint32 *>(data);
            SDL_Rect area{.x = 0, .y = 0, .w = (int)width, .h = (int)height};
            const auto &fb = framebuffers[currBackFramebuffer ^ 1u];
            for (uint32 y = 0; y < height; y++) {
                std::copy_n(&fb[y * width], width, &pixelData[y * pitch / sizeof(uint32)]);
            }
        }

        // Video sync
        bool videoSync = false;
        bool expectFrame = false;
        util::Event frameReadyEvent{false};   // emulator has written a new frame to the staging buffer
        util::Event frameRequestEvent{false}; // GUI ready for the next frame
        std::chrono::steady_clock::time_point nextFrameTarget{};    // target time for next frame
        std::chrono::steady_clock::time_point nextEmuFrameTarget{}; // target time for next frame in emu thread
        std::chrono::steady_clock::duration frameInterval{};        // interval between frames

        // Duplicate frames in fullscreen mode to maintain high GUI frame rate when running at low speeds
        uint64 dupGUIFrames = 0;
        uint64 dupGUIFrameCounter = 0;

        uint64 VDP2Frames = 0;
        uint64 VDP1Frames = 0;
        uint64 VDP1DrawCalls = 0;

        uint64 lastVDP2Frames = 0;
        uint64 lastVDP1Frames = 0;
        uint64 lastVDP1DrawCalls = 0;
    } screen;

    struct EmuSpeed {
        // Primary and alternate speed factors
        std::array<double, 2> speedFactors = {1.0, 0.5};

        // Use the primary (false) or alternate (true) speed factor.
        // Effectively an index into the speedFactors array.
        bool altSpeed = false;

        // Whether emulation speed is limited.
        bool limitSpeed = true;

        // Retrieves the currently selected speed factor.
        // Doesn't take into account the speed limit flag.
        [[nodiscard]] double GetCurrentSpeedFactor() const {
            return speedFactors[altSpeed];
        }

        // Determines if sync to audio should be used given the current speed settings.
        // Sync to audio is enabled when the speed is limited to 1.0 or less.
        [[nodiscard]] bool ShouldSyncToAudio() const {
            return limitSpeed && GetCurrentSpeedFactor() <= 1.0;
        }
    } emuSpeed;

    bool paused = false;

    input::InputContext inputContext;

    struct Input2D {
        float x, y;
    };

    struct ControlPadInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        std::unordered_map<input::InputElement, Input2D> dpad2DInputs;

        void UpdateDPad(float sensitivity) {
            using Button = ymir::peripheral::Button;

            // Aggregate all D-Pad inputs
            float x = 0.0f;
            float y = 0.0f;
            for (auto &[_, inputs] : dpad2DInputs) {
                x += inputs.x;
                y += inputs.y;
            }

            // Clamp to -1.0..1.0
            x = std::clamp(x, -1.0f, 1.0f);
            y = std::clamp(y, -1.0f, 1.0f);

            // Convert combined input into D-Pad button states
            buttons |= Button::Left | Button::Right | Button::Up | Button::Down;
            buttons &=
                ~input::AnalogToDigital2DAxis(x, y, sensitivity, Button::Right, Button::Left, Button::Down, Button::Up);
        }
    };

    struct AnalogPadInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        float x = 0.0f, y = 0.0f; // analog stick: -1.0f (left/up) to 1.0f (down/right)
        float l = 0.0f, r = 0.0f; // analog triggers: 0.0f (released) to 1.0f (pressed)
        bool analogMode = true;

        std::unordered_map<input::InputElement, Input2D> dpad2DInputs;
        std::unordered_map<input::InputElement, Input2D> analogStickInputs;
        std::unordered_map<input::InputElement, float> analogLInputs;
        std::unordered_map<input::InputElement, float> analogRInputs;

        void UpdateDPad(float sensitivity) {
            using Button = ymir::peripheral::Button;

            // Aggregate all D-Pad inputs
            float x = 0.0f;
            float y = 0.0f;
            for (auto &[_, inputs] : dpad2DInputs) {
                x += inputs.x;
                y += inputs.y;
            }

            // Also aggregate analog stick inputs if in digital mode
            if (!analogMode) {
                for (auto &[_, inputs] : analogStickInputs) {
                    x += inputs.x;
                    y += inputs.y;
                }
            }

            // Clamp to -1.0..1.0
            x = std::clamp(x, -1.0f, 1.0f);
            y = std::clamp(y, -1.0f, 1.0f);

            // Convert combined input into D-Pad button states
            buttons |= Button::Left | Button::Right | Button::Up | Button::Down;
            buttons &=
                ~input::AnalogToDigital2DAxis(x, y, sensitivity, Button::Right, Button::Left, Button::Down, Button::Up);
        }

        void UpdateAnalogStick(float sensitivity) {
            // Aggregate all analog stick inputs
            x = 0.0f;
            y = 0.0f;
            for (auto &[_, inputs] : analogStickInputs) {
                x += inputs.x;
                y += inputs.y;
            }

            // Clamp to -1.0..1.0
            x = std::clamp(x, -1.0f, 1.0f);
            y = std::clamp(y, -1.0f, 1.0f);

            // Also update DPad in digital mode
            if (!analogMode) {
                UpdateDPad(sensitivity);
            }
        }

        void UpdateAnalogTriggers() {
            // Aggregate all analog trigger inputs
            l = 0.0f;
            r = 0.0f;
            for (auto &[_, inputs] : analogLInputs) {
                l += inputs;
            }
            for (auto &[_, inputs] : analogRInputs) {
                r += inputs;
            }

            // Clamp to 0.0..1.0
            l = std::clamp(l, 0.0f, 1.0f);
            r = std::clamp(r, 0.0f, 1.0f);
        }
    };

    struct ArcadeRacerInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        float rawWheel = 0.0f; // raw analog wheel value: -1.0f (left) to 1.0f (right)
        float wheel = 0.0f;    // analog wheel value with sensitivity curve applied

        float sensitivity = 1.0f; // sensitivity exponent

        std::unordered_map<input::InputElement, Input2D> dpad2DInputs;
        std::unordered_map<input::InputElement, float> analogWheelInputs;

        void UpdateAnalogWheel() {
            // Aggregate all analog wheel inputs
            rawWheel = 0.0f;
            for (auto &[_, inputs] : analogWheelInputs) {
                rawWheel += inputs;
            }

            // Clamp to -1.0..1.0
            rawWheel = std::clamp(rawWheel, -1.0f, 1.0f);

            // Apply sensitivity
            wheel = input::ApplySensitivity(rawWheel, sensitivity);
        }
    };

    struct MissionStickInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        struct Stick {
            float x = 0.0f, y = 0.0f; // analog stick: -1.0f (left/up) to 1.0f (down/right)
            float z = 0.0f;           // analog throttle: 0.0f (minimum/down) to 1.0f (maximum/up)

            std::unordered_map<input::InputElement, Input2D> analogStickInputs;
            std::unordered_map<input::InputElement, float> analogThrottleInputs;
        };
        std::array<Stick, 2> sticks{};
        bool sixAxisMode = true;

        // Digital throttles managed by the [Main|Sub]Throttle[Up|Down|Max|Min] actions
        std::array<float, 2> digitalThrottles{0.0f, 0.0f};

        void UpdateAnalogStick(bool sub) {
            auto &stick = sticks[sub];

            // Aggregate all analog stick inputs
            stick.x = 0.0f;
            stick.y = 0.0f;
            for (auto &[_, inputs] : stick.analogStickInputs) {
                stick.x += inputs.x;
                stick.y += inputs.y;
            }

            // Clamp to -1.0..1.0
            stick.x = std::clamp(stick.x, -1.0f, 1.0f);
            stick.y = std::clamp(stick.y, -1.0f, 1.0f);
        }

        void UpdateAnalogThrottle(bool sub) {
            auto &stick = sticks[sub];

            // Aggregate all analog throttle inputs
            stick.z = 0.0f;
            for (auto &[_, inputs] : stick.analogThrottleInputs) {
                stick.z += inputs;
            }
            stick.z += digitalThrottles[sub];

            // Clamp to 0.0..1.0
            stick.z = std::clamp(stick.z, 0.0f, 1.0f);
        }
    };

    struct VirtuaGunInput {
        bool start = false;
        bool trigger = false;
        bool reload = false;

        // Accumulated movement inputs.
        float inputX = 0.0f;
        float inputY = 0.0f;

        // Cursor movement speed (in screen-space pixels per second).
        float speed = 200.0f;
        bool speedBoost = false;
        float speedBoostFactor = 2.0f;

        // Whether to treat mouse inputs as absolute or relative.
        bool mouseAbsolute = false;
        bool prevMouseAbsolute = false;

        Input2D mouseInput{0.0f, 0.0f};
        std::unordered_map<input::InputElement, Input2D> otherInputs;

        // Current window-space coordinates. 0x0 = top-left of window.
        // Floating-point to account for sensitivity/acceleration.
        float posX = 0.0f;
        float posY = 0.0f;

        // Initialize (recenter) window-space coordinates?
        bool init = true;

        void UpdateInputs() {
            inputX = 0.0f;
            inputY = 0.0f;

            // Ignore relative inputs in absolute mouse mode
            if (mouseAbsolute) {
                return;
            }

            // Aggregate all inputs
            for (auto &[_, input] : otherInputs) {
                inputX += input.x;
                inputY += input.y;
            }

            // Clamp to -1.0..1.0
            inputX = std::clamp(inputX, -1.0f, 1.0f);
            inputY = std::clamp(inputY, -1.0f, 1.0f);
        }

        void SetPosition(float x, float y) {
            if (!mouseAbsolute) {
                posX = x;
                posY = y;
            }
        }

        // Increment position by the accumulated movement inputs and clamp to the given area.
        void UpdatePosition(float timeDelta, float screenScale, float left, float top, float right, float bottom) {
            if (prevMouseAbsolute && !mouseAbsolute) {
                mouseInput.x = mouseInput.y = 0.0f;
            }
            prevMouseAbsolute = mouseAbsolute;

            if (mouseAbsolute) {
                posX = mouseInput.x = std::clamp<float>(mouseInput.x, left, right);
                posY = mouseInput.y = std::clamp<float>(mouseInput.y, top, bottom);
            } else {
                const float factor = timeDelta * screenScale * speed * (speedBoost ? speedBoostFactor : 1.0f);
                posX += inputX * factor + mouseInput.x;
                posY += inputY * factor + mouseInput.y;
                posX = std::clamp<float>(posX, left, right);
                posY = std::clamp<float>(posY, top, bottom);
                mouseInput.x = mouseInput.y = 0.0f;
            }
        }
    };

    struct ShuttleMouseInput {
        bool start = false;
        bool left = false;
        bool middle = false;
        bool right = false;

        // Accumulated movement inputs.
        float inputX = 0.0f;
        float inputY = 0.0f;

        // Cursor movement speed (in screen-space pixels per second).
        float speed = 200.0f;
        bool speedBoost = false;
        float speedBoostFactor = 2.0f;
        float relInputSensitivity = 2.0f;

        Input2D relInput{0.0f, 0.0f};
        std::unordered_map<input::InputElement, Input2D> otherInputs;

        void UpdateInputs() {
            inputX = 0.0f;
            inputY = 0.0f;

            // Aggregate all inputs
            for (auto &[_, input] : otherInputs) {
                inputX += input.x;
                inputY += input.y;
            }

            // Clamp to -1.0..1.0
            inputX = std::clamp(inputX, -1.0f, 1.0f);
            inputY = std::clamp(inputY, -1.0f, 1.0f);

            // Apply speed boost
            if (speedBoost) {
                inputX *= speedBoostFactor;
                inputY *= speedBoostFactor;
            }

            // Compute final inputs
            inputX = inputX * speed + relInput.x * relInputSensitivity;
            inputY = inputY * speed + relInput.y * relInputSensitivity;

            // Reset relative inputs
            relInput.x = relInput.y = 0.0f;
        }
    };

    std::array<ControlPadInput, 2> controlPadInputs;
    std::array<AnalogPadInput, 2> analogPadInputs;
    std::array<ArcadeRacerInput, 2> arcadeRacerInputs;
    std::array<MissionStickInput, 2> missionStickInputs;
    std::array<VirtuaGunInput, 2> virtuaGunInputs;
    std::array<ShuttleMouseInput, 2> shuttleMouseInputs;

    int gameControllerDBCount = 0;

    Profile profile;
    UpdateChecker updateChecker;

    struct Updates {
        bool inProgress = false;
        std::optional<UpdateInfo> latestStable;
        std::optional<UpdateInfo> latestNightly;
    } updates;

    struct TargetUpdate {
        UpdateInfo info;
        ReleaseChannel channel;
    };
    std::optional<TargetUpdate> targetUpdate = std::nullopt;

    ROMManager romManager;
    std::filesystem::path iplRomPath;
    const ymir::db::IPLROMInfo *iplRomInfo = nullptr;
    std::filesystem::path cdbRomPath;

    RewindBuffer rewindBuffer;
    bool rewinding = false;

    // Certain GUI interactions require synchronization with the emulator thread, especially when dealing with
    // dynamically allocated objects:
    // - Cartridges
    // - Discs
    // - Peripherals
    // - ROM manager
    // - Backup memories
    // - Messages
    // - Breakpoint management
    // These locks must be held by the emulator thread whenever the object instances are to be replaced or modified.
    // The GUI must hold these locks when accessing these objects to ensure the emulator thread doesn't destroy them.
    mutable struct Locks {
        std::mutex cart;
        std::mutex disc;
        std::mutex peripherals;
        std::mutex romManager;
        std::mutex backupRAM;
        std::mutex messages;
        std::mutex breakpoints;
        std::mutex watchpoints;
        std::mutex updates;
        std::mutex targetUpdate;
        std::mutex renderer;
    } locks;

    struct State {
        std::filesystem::path loadedDiscImagePath;
        std::filesystem::path loadedDiscDrivePath;
        std::deque<std::filesystem::path> recentDiscs;
    } state;

    struct Tracers {
        SH2Tracer masterSH2;
        SH2Tracer slaveSH2;
        SCUTracer SCU;
        SCSPTracer SCSP;
        CDBlockTracer CDBlock;
        CDDriveTracer CDDrive;
        YGRTracer YGR;
    } tracers;

    struct Images {
        struct Image {
            gfx::GUITextureHandle texture = gfx::kInvalidGUITextureHandle;
            ImVec2 size;
        };

        Image ymirLogo;
    } images;

    struct EventQueues {
        moodycamel::BlockingConcurrentQueue<EmuEvent> emulator;
        moodycamel::BlockingConcurrentQueue<GUIEvent> gui;
    } eventQueues;

    // Circular buffer of messages to be displayed
    mutable struct Messages {
        std::array<Message, 10000> list{};
        size_t count = 0;
        size_t head = 0;

        void Add(std::string message) {
            Message msg{.message = message,
                        .timestamp = std::chrono::steady_clock::now(),
                        .sysTime = std::chrono::system_clock::now()};
            if (count < list.size()) {
                list[count++] = msg;
            } else {
                list[head++] = msg;
                if (head == list.size()) {
                    head = 0;
                }
            }
        }

        [[nodiscard]] size_t Count() const {
            return count;
        }

        [[nodiscard]] const Message *const Get(size_t index) const {
            if (index >= count) {
                return nullptr;
            }
            return &list[(head + index) % list.size()];
        }
    } messages;

    struct Debuggers {
        bool dirty = false;
        std::chrono::steady_clock::time_point dirtyTimestamp;

        void MakeDirty(bool dirty = true) {
            this->dirty = dirty;
            dirtyTimestamp = std::chrono::steady_clock::now();
        }
    } debuggers;

    // -----------------------------------------------------------------------------------------------------------------
    // Convenience methods

    explicit SharedContext();
    ~SharedContext();

    void DisplayMessage(std::string message) const {
        std::unique_lock lock{locks.messages};
        devlog::info<grp::base>("{}", message);
        messages.Add(message);
    }

    void EnqueueEvent(EmuEvent &&event) {
        eventQueues.emulator.enqueue(std::move(event));
    }

    void EnqueueEvent(GUIEvent &&event) {
        eventQueues.gui.enqueue(std::move(event));
    }

    std::filesystem::path GetGameFileName(bool oldStyle = false) const;

    std::filesystem::path GetInternalBackupRAMPath() const;
    std::filesystem::path GetPerGameExternalBackupRAMPath(ymir::bup::BackupMemorySize bupSize) const;

    std::filesystem::path GetPersistentSMPCDataPath() const;

    // Retrieves the current display if one is selected.
    // Returns the primary display if the "current display" option is selected.
    SDL_DisplayID GetSelectedDisplay() const;
};

} // namespace app
