#pragma once

#include <app/services/disc_service.hpp>

#include <ymir/hw/scsp/scsp_midi_defs.hpp>
#include <ymir/sys/backup_ram.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

namespace app {

struct SharedContext;

struct EmuEvent {
    enum class Type {
        FactoryReset,
        HardReset,
        SoftReset,
        SetResetButton,

        SetPaused,
        ForwardFrameStep,
        ReverseFrameStep,
        StepMSH2,
        StepSSH2,

        OpenCloseTray,
        LoadDisc,
        ApplyDisc,
        OpenHostDevice,
        EjectDisc,

        RemoveCartridge,

        ReplaceInternalBackupMemory,
        ReplaceExternalBackupMemory,

        RunFunction,

        ReceiveMidiInput,

        SetThreadPriority,

        Shutdown,
    };

    Type type;

    std::variant<std::monostate, ymir::scsp::MidiMessage, bool, std::string, std::filesystem::path,
                 ymir::bup::BackupMemory, std::function<void(SharedContext &)>, services::DiscService::AsyncLoadState>
        value;
};

constexpr std::string_view EmuEventToString(EmuEvent::Type type) {
    switch (type) {
    case EmuEvent::Type::FactoryReset: return "FactoryReset";
    case EmuEvent::Type::HardReset: return "HardReset";
    case EmuEvent::Type::SoftReset: return "SoftReset";
    case EmuEvent::Type::SetResetButton: return "SetResetButton";
    case EmuEvent::Type::SetPaused: return "SetPaused";
    case EmuEvent::Type::ForwardFrameStep: return "ForwardFrameStep";
    case EmuEvent::Type::ReverseFrameStep: return "ReverseFrameStep";
    case EmuEvent::Type::StepMSH2: return "StepMSH2";
    case EmuEvent::Type::StepSSH2: return "StepSSH2";
    case EmuEvent::Type::OpenCloseTray: return "OpenCloseTray";
    case EmuEvent::Type::LoadDisc: return "LoadDisc";
    case EmuEvent::Type::ApplyDisc: return "ApplyDisc";
    case EmuEvent::Type::OpenHostDevice: return "OpenHostDevice";
    case EmuEvent::Type::EjectDisc: return "EjectDisc";
    case EmuEvent::Type::RemoveCartridge: return "RemoveCartridge";
    case EmuEvent::Type::ReplaceInternalBackupMemory: return "ReplaceInternalBackupMemory";
    case EmuEvent::Type::ReplaceExternalBackupMemory: return "ReplaceExternalBackupMemory";
    case EmuEvent::Type::RunFunction: return "RunFunction";
    case EmuEvent::Type::ReceiveMidiInput: return "ReceiveMidiInput";
    case EmuEvent::Type::SetThreadPriority: return "SetThreadPriority";
    case EmuEvent::Type::Shutdown: return "Shutdown";
    default: return "Unknown";
    }
}

inline std::ostream &operator<<(std::ostream &os, EmuEvent::Type type) {
    return os << EmuEventToString(type);
}

inline std::ostream &operator<<(std::ostream &os, const EmuEvent &event) {
    return os << "EmuEvent{ type: " << event.type << " }";
}

} // namespace app
