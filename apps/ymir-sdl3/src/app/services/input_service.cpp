#include "input_service.hpp"

#include <app/actions.hpp>
#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>
#include <app/services/disc_service.hpp>
#include <app/services/mouse_capture_service.hpp>
#include <app/services/save_state_service.hpp>
#include <app/ui/widgets/input_widgets.hpp>

#include <ymir/sys/saturn.hpp>

#include <util/math.hpp>
#include <ymir/util/callback.hpp>
#include <ymir/util/dev_log.hpp>

#include <fmt/format.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>

using clk = std::chrono::steady_clock;

namespace app::services {

InputService::InputService(SharedContext &context, Settings &settings, InputServiceCallbacks callbacks)
    : m_context(context)
    , m_settings(settings)
    , m_callbacks(std::move(callbacks)) {

    auto &inputContext = m_context.inputContext;

    // General
    {
        inputContext.SetTriggerHandler(actions::general::OpenSettings,
                                       [&](void *, const input::InputElement &) { m_callbacks.openSettings(); });
        inputContext.SetTriggerHandler(actions::general::ToggleWindowedVideoOutput,
                                       [&](void *, const input::InputElement &) {
                                           m_settings.video.displayVideoOutputInWindow ^= true;
                                           ImGui::SetNextFrameWantCaptureKeyboard(false);
                                           ImGui::SetNextFrameWantCaptureMouse(false);
                                       });
        inputContext.SetTriggerHandler(actions::general::ToggleFullScreen, [&](void *, const input::InputElement &) {
            m_settings.video.fullScreen = !m_settings.video.fullScreen;
            m_settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::general::ShowMessageHistory,
                                       [&](void *, const input::InputElement &) { m_callbacks.showMessageHistory(); });
        inputContext.SetTriggerHandler(actions::general::TakeScreenshot, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::gui::TakeScreenshot());
        });
        inputContext.SetTriggerHandler(actions::general::ExitApp, [&](void *, const input::InputElement &) {
            SDL_Event quitEvent{.type = SDL_EVENT_QUIT};
            SDL_PushEvent(&quitEvent);
        });
    }

    // View
    {
        inputContext.SetTriggerHandler(actions::view::ToggleFrameRateOSD, [&](void *, const input::InputElement &) {
            m_settings.gui.showFrameRateOSD = !m_settings.gui.showFrameRateOSD;
            m_settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::NextFrameRateOSDPos, [&](void *, const input::InputElement &) {
            const uint32 pos = static_cast<uint32>(m_settings.gui.frameRateOSDPosition);
            const uint32 nextPos = pos >= 3 ? 0 : pos + 1;
            m_settings.gui.frameRateOSDPosition = static_cast<Settings::GUI::FrameRateOSDPosition>(nextPos);
            m_settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::PrevFrameRateOSDPos, [&](void *, const input::InputElement &) {
            const uint32 pos = static_cast<uint32>(m_settings.gui.frameRateOSDPosition);
            const uint32 prevPos = pos == 0 ? 3 : pos - 1;
            m_settings.gui.frameRateOSDPosition = static_cast<Settings::GUI::FrameRateOSDPosition>(prevPos);
            m_settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::RotateScreenCW, [&](void *, const input::InputElement &) {
            const uint32 rot = static_cast<uint32>(m_settings.video.rotation);
            const uint32 nextRot = rot >= 3 ? 0 : rot + 1;
            m_settings.video.rotation = static_cast<Settings::Video::DisplayRotation>(nextRot);
            m_settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::RotateScreenCCW, [&](void *, const input::InputElement &) {
            const uint32 rot = static_cast<uint32>(m_settings.video.rotation);
            const uint32 prevRot = rot == 0 ? 3 : rot - 1;
            m_settings.video.rotation = static_cast<Settings::Video::DisplayRotation>(prevRot);
            m_settings.MakeDirty();
        });
    }

    // Audio
    {
        inputContext.SetTriggerHandler(actions::audio::ToggleMute, [&](void *, const input::InputElement &) {
            m_context.lastVolumeChangeTime = clk::now();
            m_settings.audio.mute = !m_settings.audio.mute;
            m_settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::audio::IncreaseVolume, [&](void *, const input::InputElement &) {
            m_context.lastVolumeChangeTime = clk::now();
            m_settings.audio.volume = std::min(m_settings.audio.volume + 0.1f, 1.0f);
            m_settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::audio::DecreaseVolume, [&](void *, const input::InputElement &) {
            m_context.lastVolumeChangeTime = clk::now();
            m_settings.audio.volume = std::max(m_settings.audio.volume - 0.1f, 0.0f);
            m_settings.MakeDirty();
        });
    }

    // CD drive
    {
        inputContext.SetTriggerHandler(actions::cd_drive::LoadDisc, [&](void *, const input::InputElement &) {
            if (auto discService = m_context.serviceLocator.Get<DiscService>()) {
                discService->OpenLoadDiscDialog();
            }
        });
        inputContext.SetTriggerHandler(actions::cd_drive::EjectDisc, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::EjectDisc());
        });
        inputContext.SetTriggerHandler(actions::cd_drive::OpenCloseTray, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::OpenCloseTray());
        });
    }

    // Save states
    {
        inputContext.SetTriggerHandler(actions::save_states::QuickLoadState, [&](void *, const input::InputElement &) {
            if (auto saveStateService = m_context.serviceLocator.Get<SaveStateService>()) {
                m_context.EnqueueEvent(events::emu::LoadState(saveStateService->CurrentSlot()));
            }
        });
        inputContext.SetTriggerHandler(actions::save_states::QuickSaveState, [&](void *, const input::InputElement &) {
            if (auto saveStateService = m_context.serviceLocator.Get<SaveStateService>()) {
                m_context.EnqueueEvent(events::emu::SaveState(saveStateService->CurrentSlot()));
            }
        });

        // Select state
        inputContext.SetTriggerHandler(actions::save_states::SelectState1, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(0);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState2, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(1);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState3, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(2);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState4, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(3);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState5, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(4);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState6, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(5);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState7, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(6);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState8, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(7);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState9, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(8);
        });
        inputContext.SetTriggerHandler(actions::save_states::SelectState10, [&](void *, const input::InputElement &) {
            m_callbacks.selectSaveStateSlot(9);
        });

        // Load state
        inputContext.SetTriggerHandler(actions::save_states::LoadState1,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(0); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState2,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(1); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState3,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(2); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState4,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(3); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState5,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(4); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState6,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(5); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState7,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(6); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState8,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(7); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState9,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(8); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState10,
                                       [&](void *, const input::InputElement &) { m_callbacks.loadSaveStateSlot(9); });

        // Save state
        inputContext.SetTriggerHandler(actions::save_states::SaveState1,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(0); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState2,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(1); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState3,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(2); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState4,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(3); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState5,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(4); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState6,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(5); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState7,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(6); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState8,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(7); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState9,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(8); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState10,
                                       [&](void *, const input::InputElement &) { m_callbacks.saveSaveStateSlot(9); });

        // Undo save/load state
        inputContext.SetTriggerHandler(actions::save_states::UndoSaveState, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::UndoSaveState());
        });
        inputContext.SetTriggerHandler(actions::save_states::UndoLoadState, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::UndoLoadState());
        });
    }

    // System
    {
        inputContext.SetTriggerHandler(actions::sys::HardReset, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::HardReset());
        });
        inputContext.SetTriggerHandler(actions::sys::SoftReset, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SoftReset());
        });
        inputContext.SetButtonHandler(actions::sys::ResetButton,
                                      [&](void *, const input::InputElement &, bool actuated) {
                                          m_context.EnqueueEvent(events::emu::SetResetButton(actuated));
                                      });
    }

    // Emulation
    {
        inputContext.SetButtonHandler(actions::emu::TurboSpeed,
                                      [&](void *, const input::InputElement &, bool actuated) {
                                          m_context.emuSpeed.limitSpeed = !actuated;
                                          m_context.audioSystem.SetSync(m_context.emuSpeed.ShouldSyncToAudio());
                                      });
        inputContext.SetTriggerHandler(actions::emu::TurboSpeedHold, [&](void *, const input::InputElement &) {
            m_context.emuSpeed.limitSpeed ^= true;
            m_context.audioSystem.SetSync(m_context.emuSpeed.ShouldSyncToAudio());
        });
        inputContext.SetTriggerHandler(actions::emu::ToggleAlternateSpeed, [&](void *, const input::InputElement &) {
            auto &general = m_settings.general;
            general.useAltSpeed = !general.useAltSpeed;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            m_settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("Using {} emulation speed: {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "alternate" : "primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::IncreaseSpeed, [&](void *, const input::InputElement &) {
            auto &general = m_settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::min(util::RoundToMultiple(speed + 0.05, 0.05), 5.0);
            m_settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed increased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::DecreaseSpeed, [&](void *, const input::InputElement &) {
            auto &general = m_settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::max(util::RoundToMultiple(speed - 0.05, 0.05), 0.1);
            m_settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed decreased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::IncreaseSpeedLarge, [&](void *, const input::InputElement &) {
            auto &general = m_settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::min(util::RoundToMultiple(speed + 0.25, 0.05), 5.0);
            m_settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed increased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::DecreaseSpeedLarge, [&](void *, const input::InputElement &) {
            auto &general = m_settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::max(util::RoundToMultiple(speed - 0.25, 0.05), 0.1);
            m_settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed decreased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::ResetSpeed, [&](void *, const input::InputElement &) {
            auto &general = m_settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = general.useAltSpeed.Get() ? 0.5 : 1.0;
            m_settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed reset to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });

        inputContext.SetTriggerHandler(actions::emu::PauseResume, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SetPaused(!m_context.paused));
        });
        inputContext.SetTriggerHandler(actions::emu::ForwardFrameStep, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::ForwardFrameStep());
        });
        inputContext.SetTriggerHandler(actions::emu::ReverseFrameStep, [&](void *, const input::InputElement &) {
            if (m_context.rewindBuffer.IsRunning()) {
                m_context.EnqueueEvent(events::emu::ReverseFrameStep());
            }
        });
        inputContext.SetButtonHandler(actions::emu::Rewind, [&](void *, const input::InputElement &, bool actuated) {
            m_context.rewinding = actuated;
        });

        inputContext.SetButtonHandler(actions::emu::ToggleRewindBuffer,
                                      [&](void *, const input::InputElement &, bool actuated) {
                                          if (actuated) {
                                              m_callbacks.toggleRewindBuffer();
                                          }
                                      });
    }

    // Debugger
    {
        inputContext.SetTriggerHandler(actions::dbg::ToggleDebugTrace, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(!m_context.saturn.instance->IsDebugTracingEnabled()));
        });
        inputContext.SetTriggerHandler(actions::dbg::DumpMemory, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::DumpMemory());
        });
    }

    // Saturn Control Pad
    {
        using Button = ymir::peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDPadButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(
                action, [=, this](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    if (actuated) {
                        dpadInput.x = x;
                        dpadInput.y = y;
                    } else {
                        dpadInput.x = 0.0f;
                        dpadInput.y = 0.0f;
                    }
                    input.UpdateDPad(m_settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        auto registerDPad2DAxis = [&](input::Action action) {
            inputContext.SetAxis2DHandler(
                action, [this](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    dpadInput.x = x;
                    dpadInput.y = y;
                    input.UpdateDPad(m_settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        registerButton(actions::control_pad::A, Button::A);
        registerButton(actions::control_pad::B, Button::B);
        registerButton(actions::control_pad::C, Button::C);
        registerButton(actions::control_pad::X, Button::X);
        registerButton(actions::control_pad::Y, Button::Y);
        registerButton(actions::control_pad::Z, Button::Z);
        registerButton(actions::control_pad::Start, Button::Start);
        registerButton(actions::control_pad::L, Button::L);
        registerButton(actions::control_pad::R, Button::R);
        registerDPadButton(actions::control_pad::Up, 0.0f, -1.0f);
        registerDPadButton(actions::control_pad::Down, 0.0f, +1.0f);
        registerDPadButton(actions::control_pad::Left, -1.0f, 0.0f);
        registerDPadButton(actions::control_pad::Right, +1.0f, 0.0f);
        registerDPad2DAxis(actions::control_pad::DPad);
    }

    // Saturn 3D Control Pad
    {
        using Button = ymir::peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDPadButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(action,
                                          [=, this](void *context, const input::InputElement &element, bool actuated) {
                                              auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                                              auto &dpadInput = input.dpad2DInputs[element];
                                              if (actuated) {
                                                  dpadInput.x = x;
                                                  dpadInput.y = y;
                                              } else {
                                                  dpadInput.x = 0.0f;
                                                  dpadInput.y = 0.0f;
                                              }
                                              input.UpdateDPad(m_settings.input.gamepad.analogToDigitalSensitivity);
                                          });
        };

        auto registerDPad2DAxis = [&](input::Action action) {
            inputContext.SetAxis2DHandler(action,
                                          [this](void *context, const input::InputElement &element, float x, float y) {
                                              auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                                              auto &dpadInput = input.dpad2DInputs[element];
                                              dpadInput.x = x;
                                              dpadInput.y = y;
                                              input.UpdateDPad(m_settings.input.gamepad.analogToDigitalSensitivity);
                                          });
        };

        auto registerAnalogStick = [&](input::Action action) {
            inputContext.SetAxis2DHandler(
                action, [this](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                    auto &analogInput = input.analogStickInputs[element];
                    analogInput.x = x;
                    analogInput.y = y;
                    input.UpdateAnalogStick(m_settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        auto registerDigitalTrigger = [&](input::Action action, bool which /*false=L, true=R*/) {
            inputContext.SetButtonHandler(action,
                                          [=](void *context, const input::InputElement &element, bool actuated) {
                                              auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                                              auto &map = which ? input.analogRInputs : input.analogLInputs;
                                              if (actuated) {
                                                  map[element] = 1.0f;
                                              } else {
                                                  map[element] = 0.0f;
                                              }
                                              input.UpdateAnalogTriggers();
                                          });
        };

        auto registerAnalogTrigger = [&](input::Action action, bool which /*false=L, true=R*/) {
            inputContext.SetAxis1DHandler(action,
                                          [which](void *context, const input::InputElement &element, float value) {
                                              auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                                              auto &map = which ? input.analogRInputs : input.analogLInputs;
                                              map[element] = value;
                                              input.UpdateAnalogTriggers();
                                          });
        };

        auto registerModeSwitch = [&](input::Action action) {
            inputContext.SetTriggerHandler(action, [&](void *context, const input::InputElement &element) {
                auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                input.analogMode ^= true;
                // Update D-Pad status in case an analog stick was pushed while switchin modes
                input.UpdateDPad(m_settings.input.gamepad.analogToDigitalSensitivity);
                int portNum = (context == &m_context.analogPadInputs[0]) ? 1 : 2;
                m_context.DisplayMessage(fmt::format("Port {} 3D Control Pad switched to {} mode", portNum,
                                                     (input.analogMode ? "analog" : "digital")));
            });
        };

        registerButton(actions::analog_pad::A, Button::A);
        registerButton(actions::analog_pad::B, Button::B);
        registerButton(actions::analog_pad::C, Button::C);
        registerButton(actions::analog_pad::X, Button::X);
        registerButton(actions::analog_pad::Y, Button::Y);
        registerButton(actions::analog_pad::Z, Button::Z);
        registerButton(actions::analog_pad::Start, Button::Start);
        registerDigitalTrigger(actions::analog_pad::L, false);
        registerDigitalTrigger(actions::analog_pad::R, true);
        registerDPadButton(actions::analog_pad::Up, 0.0f, -1.0f);
        registerDPadButton(actions::analog_pad::Down, 0.0f, +1.0f);
        registerDPadButton(actions::analog_pad::Left, -1.0f, 0.0f);
        registerDPadButton(actions::analog_pad::Right, +1.0f, 0.0f);
        registerDPad2DAxis(actions::analog_pad::DPad);
        registerAnalogStick(actions::analog_pad::AnalogStick);
        registerAnalogTrigger(actions::analog_pad::AnalogL, false);
        registerAnalogTrigger(actions::analog_pad::AnalogR, true);
        registerModeSwitch(actions::analog_pad::SwitchMode);
    }

    // Arcade Racer controller
    {
        using Button = ymir::peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::ArcadeRacerInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDigitalWheel = [&](input::Action action, bool which /*false=L, true=R*/) {
            inputContext.SetButtonHandler(
                action, [=](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::ArcadeRacerInput *>(context);
                    auto &map = input.analogWheelInputs;
                    if (actuated) {
                        map[element] = which ? 1.0f : -1.0f;
                    } else {
                        map[element] = 0.0f;
                    }
                    input.UpdateAnalogWheel();
                });
        };

        auto registerAnalogWheel = [&](input::Action action) {
            inputContext.SetAxis1DHandler(action, [](void *context, const input::InputElement &element, float value) {
                auto &input = *reinterpret_cast<SharedContext::ArcadeRacerInput *>(context);
                auto &map = input.analogWheelInputs;
                map[element] = value;
                input.UpdateAnalogWheel();
            });
        };

        registerButton(actions::arcade_racer::A, Button::A);
        registerButton(actions::arcade_racer::B, Button::B);
        registerButton(actions::arcade_racer::C, Button::C);
        registerButton(actions::arcade_racer::X, Button::X);
        registerButton(actions::arcade_racer::Y, Button::Y);
        registerButton(actions::arcade_racer::Z, Button::Z);
        registerButton(actions::arcade_racer::Start, Button::Start);
        registerButton(actions::arcade_racer::GearUp, Button::Down); // yes, it's reversed
        registerButton(actions::arcade_racer::GearDown, Button::Up);
        registerDigitalWheel(actions::arcade_racer::WheelLeft, false);
        registerDigitalWheel(actions::arcade_racer::WheelRight, true);
        registerAnalogWheel(actions::arcade_racer::AnalogWheel);

        auto makeSensObserver = [&](const int index) {
            return [=, this](float value) {
                m_context.arcadeRacerInputs[index].sensitivity = value;
                m_context.arcadeRacerInputs[index].UpdateAnalogWheel();
            };
        };

        for (int i = 0; i < 2; ++i) {
            m_settings.input.ports[i].arcadeRacer.sensitivity.ObserveAndNotify(makeSensObserver(i));
        }
    }

    // Mission Stick controller
    {
        using Button = ymir::peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDigitalStick = [&](input::Action action, bool sub /*false=main, true=sub*/, float x, float y) {
            inputContext.SetButtonHandler(
                action, [=](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                    auto &analogInput = input.sticks[sub].analogStickInputs[element];
                    if (actuated) {
                        analogInput.x = x;
                        analogInput.y = y;
                    } else {
                        analogInput.x = 0.0f;
                        analogInput.y = 0.0f;
                    }
                    input.UpdateAnalogStick(sub);
                });
        };

        auto registerAnalogStick = [&](input::Action action, bool sub /*false=main, true=sub*/) {
            inputContext.SetAxis2DHandler(
                action, [sub](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                    auto &analogInput = input.sticks[sub].analogStickInputs[element];
                    analogInput.x = x;
                    analogInput.y = y;
                    input.UpdateAnalogStick(sub);
                });
        };

        auto registerDigitalThrottle = [&](input::Action action, bool sub /*false=main, true=sub*/, float delta) {
            inputContext.SetTriggerHandler(action, [sub, delta](void *context, const input::InputElement &element) {
                auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                auto &analogInput = input.digitalThrottles[sub];
                analogInput = std::clamp(analogInput + delta, 0.0f, 1.0f);
                input.UpdateAnalogThrottle(sub);
            });
        };

        auto registerAnalogThrottle = [&](input::Action action, bool sub /*false=main, true=sub*/) {
            inputContext.SetAxis1DHandler(
                action, [sub](void *context, const input::InputElement &element, float value) {
                    auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                    auto &analogInput = input.sticks[sub].analogThrottleInputs[element];
                    analogInput = value;
                    input.UpdateAnalogThrottle(sub);
                });
        };

        auto registerModeSwitch = [&](input::Action action) {
            inputContext.SetTriggerHandler(action, [&](void *context, const input::InputElement &element) {
                auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                input.sixAxisMode ^= true;
                int portNum = (context == &m_context.missionStickInputs[0]) ? 1 : 2;
                m_context.DisplayMessage(fmt::format("Port {} Mission Stick switched to {} mode", portNum,
                                                     (input.sixAxisMode ? "six-axis" : "three-axis")));
            });
        };

        registerButton(actions::mission_stick::A, Button::A);
        registerButton(actions::mission_stick::B, Button::B);
        registerButton(actions::mission_stick::C, Button::C);
        registerButton(actions::mission_stick::X, Button::X);
        registerButton(actions::mission_stick::Y, Button::Y);
        registerButton(actions::mission_stick::Z, Button::Z);
        registerButton(actions::mission_stick::L, Button::L);
        registerButton(actions::mission_stick::R, Button::R);
        registerButton(actions::mission_stick::Start, Button::Start);
        registerDigitalStick(actions::mission_stick::MainUp, false, 0.0f, -1.0f);
        registerDigitalStick(actions::mission_stick::MainDown, false, 0.0f, +1.0f);
        registerDigitalStick(actions::mission_stick::MainLeft, false, -1.0f, 0.0f);
        registerDigitalStick(actions::mission_stick::MainRight, false, +1.0f, 0.0f);
        registerAnalogStick(actions::mission_stick::MainStick, false);
        registerAnalogThrottle(actions::mission_stick::MainThrottle, false);
        registerDigitalThrottle(actions::mission_stick::MainThrottleUp, false, +0.1f);
        registerDigitalThrottle(actions::mission_stick::MainThrottleDown, false, -0.1f);
        registerDigitalThrottle(actions::mission_stick::MainThrottleMax, false, +1.0f);
        registerDigitalThrottle(actions::mission_stick::MainThrottleMin, false, -1.0f);
        registerDigitalStick(actions::mission_stick::SubUp, true, 0.0f, -1.0f);
        registerDigitalStick(actions::mission_stick::SubDown, true, 0.0f, +1.0f);
        registerDigitalStick(actions::mission_stick::SubLeft, true, -1.0f, 0.0f);
        registerDigitalStick(actions::mission_stick::SubRight, true, +1.0f, 0.0f);
        registerAnalogStick(actions::mission_stick::SubStick, true);
        registerAnalogThrottle(actions::mission_stick::SubThrottle, true);
        registerDigitalThrottle(actions::mission_stick::SubThrottleUp, true, +0.1f);
        registerDigitalThrottle(actions::mission_stick::SubThrottleDown, true, -0.1f);
        registerDigitalThrottle(actions::mission_stick::SubThrottleMax, true, +1.0f);
        registerDigitalThrottle(actions::mission_stick::SubThrottleMin, true, -1.0f);
        registerModeSwitch(actions::mission_stick::SwitchMode);
    }

    // Virtua Gun controller
    {
        auto registerMoveButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(action,
                                          [=, this](void *context, const input::InputElement &element, bool actuated) {
                                              auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                              auto &moveInput = input.otherInputs[element];
                                              if (actuated) {
                                                  moveInput.x = x;
                                                  moveInput.y = y;
                                              } else {
                                                  moveInput.x = 0.0f;
                                                  moveInput.y = 0.0f;
                                              }
                                              input.UpdateInputs();
                                          });
        };

        inputContext.SetButtonHandler(actions::virtua_gun::Start,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.start = actuated;
                                      });

        inputContext.SetButtonHandler(actions::virtua_gun::Trigger,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.trigger = actuated;
                                      });

        inputContext.SetButtonHandler(actions::virtua_gun::Reload,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.reload = actuated;
                                      });

        inputContext.SetAxis2DHandler(actions::virtua_gun::Move,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          auto &moveInput = input.otherInputs[element];
                                          moveInput.x = x;
                                          moveInput.y = y;
                                          input.UpdateInputs();
                                      });

        inputContext.SetTriggerHandler(actions::virtua_gun::Recenter,
                                       [=, this](void *context, const input::InputElement &) {
                                           auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                           input.SetPosition(m_context.screen.dCenterX, m_context.screen.dCenterY);
                                       });
        inputContext.SetButtonHandler(actions::virtua_gun::SpeedBoost,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.speedBoost = actuated;
                                      });
        inputContext.SetTriggerHandler(actions::virtua_gun::SpeedToggle,
                                       [=](void *context, const input::InputElement &) {
                                           auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                           input.speedBoost ^= true;
                                       });

        inputContext.SetAxis2DHandler(actions::virtua_gun::MouseRelMove,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          if (!input.mouseAbsolute) {
                                              input.mouseInput.x += x;
                                              input.mouseInput.y += y;
                                              input.UpdateInputs();
                                          }
                                      });
        inputContext.SetAxis2DHandler(actions::virtua_gun::MouseAbsMove,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          if (input.mouseAbsolute) {
                                              input.mouseInput.x = x;
                                              input.mouseInput.y = y;
                                              input.UpdateInputs();
                                          }
                                      });

        registerMoveButton(actions::virtua_gun::Up, 0.0f, -1.0f);
        registerMoveButton(actions::virtua_gun::Down, 0.0f, +1.0f);
        registerMoveButton(actions::virtua_gun::Left, -1.0f, 0.0f);
        registerMoveButton(actions::virtua_gun::Right, +1.0f, 0.0f);

        auto &inputSettings = m_settings.input;

        for (int i = 0; i < 2; ++i) {
            inputSettings.ports[i].virtuaGun.speed.Observe(m_context.virtuaGunInputs[i].speed);
            inputSettings.ports[i].virtuaGun.speedBoostFactor.Observe(m_context.virtuaGunInputs[i].speedBoostFactor);
        }
    }

    // Shuttle Mouse controller
    {
        auto registerMoveButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(
                action, [=, this](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                    auto &moveInput = input.otherInputs[element];
                    if (actuated) {
                        moveInput.x = x;
                        moveInput.y = y;
                    } else {
                        moveInput.x = 0.0f;
                        moveInput.y = 0.0f;
                    }
                });
        };

        inputContext.SetButtonHandler(actions::shuttle_mouse::Start,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.start = actuated;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::Left,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.left = actuated;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::Middle,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.middle = actuated;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::Right,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.right = actuated;
                                      });

        inputContext.SetAxis2DHandler(actions::shuttle_mouse::Move,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          auto &moveInput = input.otherInputs[element];
                                          moveInput.x = x;
                                          moveInput.y = y;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::SpeedBoost,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.speedBoost = actuated;
                                      });
        inputContext.SetTriggerHandler(actions::shuttle_mouse::SpeedToggle,
                                       [=](void *context, const input::InputElement &) {
                                           auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                           input.speedBoost ^= true;
                                       });

        inputContext.SetAxis2DHandler(actions::shuttle_mouse::MouseRelMove,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.relInput.x += x;
                                          input.relInput.y += y;
                                      });

        registerMoveButton(actions::shuttle_mouse::MoveUp, 0.0f, -1.0f);
        registerMoveButton(actions::shuttle_mouse::MoveDown, 0.0f, +1.0f);
        registerMoveButton(actions::shuttle_mouse::MoveLeft, -1.0f, 0.0f);
        registerMoveButton(actions::shuttle_mouse::MoveRight, +1.0f, 0.0f);

        auto &inputSettings = m_settings.input;

        for (int i = 0; i < 2; ++i) {
            auto &portSettings = inputSettings.ports[i].shuttleMouse;
            auto &input = m_context.shuttleMouseInputs[i];
            portSettings.speed.Observe(input.speed);
            portSettings.speedBoostFactor.Observe(input.speedBoostFactor);
            portSettings.sensitivity.Observe(input.relInputSensitivity);
        }
    }

    RebindInputs();
}

void InputService::RebindInputs() {
    m_settings.RebindInputs();

    if (auto mouseCaptureService = m_context.serviceLocator.Get<MouseCaptureService>()) {
        m_settings.input.mouse.captureMode.ObserveAndNotify(
            [mouseCaptureService](Settings::Input::Mouse::CaptureMode) { mouseCaptureService->ReleaseAllMice(); });
    }
}

void InputService::UpdateInputs(double timeDelta) {
    // NOTE: this uses the previous frame's screen scale parameters
    const auto &settings = m_settings;
    const auto &screen = m_context.screen;
    const auto &videoSettings = settings.video;
    double scale = screen.scale;
    if (screen.doubleResH || screen.doubleResV) {
        scale *= 2.0;
    }

    float sWidth = screen.dSizeX;
    float sHeight = screen.dSizeY;

    if (videoSettings.rotation == Settings::Video::DisplayRotation::_90CW ||
        videoSettings.rotation == Settings::Video::DisplayRotation::_90CCW) {
        std::swap(sWidth, sHeight);
    }

    float sLeft = screen.dCenterX - sWidth * 0.5f;
    float sTop = screen.dCenterY - sHeight * 0.5f;
    float sRight = sLeft + sWidth;
    float sBottom = sTop + sHeight;

    for (uint32 portIndex = 0; portIndex < 2; ++portIndex) {
        auto &config = settings.input.ports[portIndex];

        if (config.type == ymir::peripheral::PeripheralType::VirtuaGun) {
            auto &input = m_context.virtuaGunInputs[portIndex];
            if (input.init && screen.dSizeX > 1 && screen.dSizeY > 1) {
                input.init = false;
                input.SetPosition(screen.dCenterX, screen.dCenterY);
            } else {
                input.UpdatePosition(timeDelta, scale, sLeft, sTop, sRight, sBottom);
            }
        } else if (config.type == ymir::peripheral::PeripheralType::ShuttleMouse) {
            auto &input = m_context.shuttleMouseInputs[portIndex];
            input.UpdateInputs();
        }
    }
}

void InputService::DrawInputs(ImDrawList *drawList) {
    const auto &settings = m_settings;
    const auto &screen = m_context.screen;

    for (uint32 portIndex = 0; portIndex < 2; ++portIndex) {
        const auto &config = settings.input.ports[portIndex];

        if (config.type == ymir::peripheral::PeripheralType::VirtuaGun) {
            auto &input = m_context.virtuaGunInputs[portIndex];
            auto &xhair = config.virtuaGun.crosshair;

            const ui::widgets::CrosshairParams params{
                .color = {xhair.color[0], xhair.color[1], xhair.color[2], xhair.color[3]},
                .radius = xhair.radius,
                .thickness = xhair.thickness,
                .rotation = xhair.rotation,

                .strokeColor = {xhair.strokeColor[0], xhair.strokeColor[1], xhair.strokeColor[2], xhair.strokeColor[3]},
                .strokeThickness = xhair.strokeThickness,
            };
            ui::widgets::Crosshair(drawList, params, {input.posX, input.posY});
        }
    }
}

std::pair<float, float> InputService::WindowToScreen(float x, float y) const {
    const auto &settings = m_settings;
    const auto &screen = m_context.screen;

    // Build 2D rotation matrix
    float a, b, c, d;
    switch (settings.video.rotation) {
        using Rot = Settings::Video::DisplayRotation;
    case Rot::Normal: a = +1.0f, b = 0.0f, c = 0.0f, d = +1.0f; break;
    case Rot::_90CW: a = 0.0f, b = -1.0f, c = +1.0f, d = 0.0f; break;
    case Rot::_180: a = -1.0f, b = 0.0f, c = 0.0f, d = -1.0f; break;
    case Rot::_90CCW: a = 0.0f, b = +1.0f, c = -1.0f, d = 0.0f; break;
    }

    const float nx = x - screen.dCenterX;
    const float ny = y - screen.dCenterY;

    // Rotate window coordinates around center of screen
    const float rx = nx * a + ny * c + screen.dCenterX;
    const float ry = nx * b + ny * d + screen.dCenterY;

    const float sCornerX = screen.dCenterX - screen.dSizeX * 0.5f;
    const float sCornerY = screen.dCenterY - screen.dSizeY * 0.5f;

    // Transform to screen space
    float sx = (rx - sCornerX) * screen.width / screen.dSizeX;
    float sy = (ry - sCornerY) * screen.height / screen.dSizeY;

    return {sx, sy};
}

template <int port>
void InputService::ReadPeripheral(ymir::peripheral::PeripheralReport &report) {
    // TODO: this is the appropriate location to capture inputs for a movie recording
    switch (report.type) {
    case ymir::peripheral::PeripheralType::ControlPad:
        report.report.controlPad.buttons = m_context.controlPadInputs[port - 1].buttons;
        break;
    case ymir::peripheral::PeripheralType::AnalogPad: //
    {
        auto &specificReport = report.report.analogPad;
        const auto &inputs = m_context.analogPadInputs[port - 1];
        specificReport.buttons = inputs.buttons;
        specificReport.analog = inputs.analogMode;
        specificReport.x = std::clamp(inputs.x * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.y = std::clamp(inputs.y * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.l = inputs.l * 255.0f;
        specificReport.r = inputs.r * 255.0f;
        break;
    }
    case ymir::peripheral::PeripheralType::ArcadeRacer: //
    {
        auto &specificReport = report.report.arcadeRacer;
        const auto &inputs = m_context.arcadeRacerInputs[port - 1];
        specificReport.buttons = inputs.buttons;
        specificReport.wheel = std::clamp(inputs.wheel * 128.0f + 128.0f, 0.0f, 255.0f);
        break;
    }
    case ymir::peripheral::PeripheralType::MissionStick: //
    {
        auto &specificReport = report.report.missionStick;
        const auto &inputs = m_context.missionStickInputs[port - 1];
        specificReport.buttons = inputs.buttons;
        specificReport.sixAxis = inputs.sixAxisMode;
        specificReport.x1 = std::clamp(inputs.sticks[0].x * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.y1 = std::clamp(inputs.sticks[0].y * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.z1 = inputs.sticks[0].z * 255.0f;
        specificReport.x2 = std::clamp(inputs.sticks[1].x * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.y2 = std::clamp(inputs.sticks[1].y * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.z2 = inputs.sticks[1].z * 255.0f;
        break;
    }
    case ymir::peripheral::PeripheralType::VirtuaGun: //
    {
        const auto &inputs = m_context.virtuaGunInputs[port - 1];
        const auto [sx, sy] = WindowToScreen(inputs.posX, inputs.posY);

        // TODO: handle overlapping trigger+reload gracefully

        auto &specificReport = report.report.virtuaGun;
        specificReport.start = inputs.start;
        specificReport.trigger = inputs.trigger;
        specificReport.reload = inputs.reload;
        specificReport.x = std::clamp<int>(sx, 1, m_context.screen.width - 1);
        specificReport.y = std::clamp<int>(sy, 1, m_context.screen.height - 1);
        break;
    }
    case ymir::peripheral::PeripheralType::ShuttleMouse: //
    {
        static constexpr float kMin = std::numeric_limits<sint16>::min();
        static constexpr float kMax = std::numeric_limits<sint16>::max();
        const auto &inputs = m_context.shuttleMouseInputs[port - 1];
        auto &specificReport = report.report.shuttleMouse;
        specificReport.start = inputs.start;
        specificReport.left = inputs.left;
        specificReport.middle = inputs.middle;
        specificReport.right = inputs.right;
        specificReport.x = std::clamp<float>(inputs.inputX, kMin, kMax);
        specificReport.y = std::clamp<float>(inputs.inputY, kMin, kMax);
        break;
    }
    default: break;
    }

    if (m_context.tracers.SMPCInput.enabled.load(std::memory_order_relaxed)) {
        m_context.tracers.SMPCInput.Record(m_context.tracers.emulationFrameIndex, static_cast<uint8>(port), report);
    }
}

// Explicit instantiations of templates for ports 1 and 2
template void InputService::ReadPeripheral<1>(ymir::peripheral::PeripheralReport &report);
template void InputService::ReadPeripheral<2>(ymir::peripheral::PeripheralReport &report);

} // namespace app::services
