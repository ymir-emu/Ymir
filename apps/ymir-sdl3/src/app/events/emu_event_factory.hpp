#pragma once

#include <ymir/sys/backup_ram.hpp>
#include <ymir/sys/clocks.hpp>

#include "app/ui/state/debug/memory_viewer_state.hpp"
#include <ymir/hw/smpc/peripheral/peripheral_defs.hpp>

#include "emu_event.hpp"

#include <filesystem>
#include <string>

namespace app::events::emu {

inline EmuEvent FactoryReset() {
    return {.type = EmuEvent::Type::FactoryReset};
}

inline EmuEvent HardReset() {
    return {.type = EmuEvent::Type::HardReset};
}

inline EmuEvent SoftReset() {
    return {.type = EmuEvent::Type::SoftReset};
}

inline EmuEvent SetResetButton(bool resetLevel) {
    return {.type = EmuEvent::Type::SetResetButton, .value = resetLevel};
}

inline EmuEvent SetPaused(bool paused) {
    return {.type = EmuEvent::Type::SetPaused, .value = paused};
}

inline EmuEvent ForwardFrameStep() {
    return {.type = EmuEvent::Type::ForwardFrameStep};
}

inline EmuEvent ReverseFrameStep() {
    return {.type = EmuEvent::Type::ReverseFrameStep};
}

inline EmuEvent StepMSH2() {
    return {.type = EmuEvent::Type::StepMSH2};
}

inline EmuEvent StepSSH2() {
    return {.type = EmuEvent::Type::StepSSH2};
}

inline EmuEvent OpenCloseTray() {
    return {.type = EmuEvent::Type::OpenCloseTray};
}

inline EmuEvent LoadDisc(std::filesystem::path path) {
    return {.type = EmuEvent::Type::LoadDisc, .value = path};
}

inline EmuEvent OpenHostDevice(std::string path) {
    return {.type = EmuEvent::Type::OpenHostDevice, .value = path};
}

inline EmuEvent EjectDisc() {
    return {.type = EmuEvent::Type::EjectDisc};
}

inline EmuEvent RemoveCartridge() {
    return {.type = EmuEvent::Type::RemoveCartridge};
}

inline EmuEvent ReplaceInternalBackupMemory(ymir::bup::BackupMemory &&bupMem) {
    return {.type = EmuEvent::Type::ReplaceInternalBackupMemory, .value = std::move(bupMem)};
}

inline EmuEvent ReplaceExternalBackupMemory(ymir::bup::BackupMemory &&bupMem) {
    return {.type = EmuEvent::Type::ReplaceExternalBackupMemory, .value = std::move(bupMem)};
}

inline EmuEvent RunFunction(std::function<void(SharedContext &)> &&fn) {
    return {.type = EmuEvent::Type::RunFunction, .value = std::move(fn)};
}

inline EmuEvent ReceiveMidiInput(double deltaTime, std::vector<uint8> &&payload) {
    return {.type = EmuEvent::Type::ReceiveMidiInput, .value = ymir::scsp::MidiMessage(deltaTime, std::move(payload))};
}

inline EmuEvent SetThreadPriority(bool boost) {
    return {.type = EmuEvent::Type::SetThreadPriority, .value = boost};
}

inline EmuEvent Shutdown() {
    return {.type = EmuEvent::Type::Shutdown};
}

// -----------------------------------------------------------------------------
// Specialized event factories

EmuEvent SetClockSpeed(ymir::sys::ClockSpeed clockSpeed);
EmuEvent SetVideoStandard(ymir::core::config::sys::VideoStandard videoStandard);
EmuEvent SetAreaCode(uint8 areaCode);

EmuEvent SetDeinterlace(bool enable);
EmuEvent SetTransparentMeshes(bool enable);
EmuEvent EnableResolutionScaling(uint32 width, uint32 height);
EmuEvent EnableResolutionScalingToTarget(uint32 width, uint32 height);
EmuEvent EnableResolutionScalingByFactor(uint32 factor);
EmuEvent DisableResolutionScaling();

EmuEvent UseNullVDPRenderer(util::Event &event);
EmuEvent SwitchVDPRenderer(bool verbose = true);

EmuEvent SetDebugTrace(bool enable);
EmuEvent DumpMemory();
EmuEvent DumpMemRegion(const ui::mem_view::MemoryViewerState &memView);

EmuEvent InsertPeripheral(uint32 port, ymir::peripheral::PeripheralType type);

EmuEvent InsertBackupMemoryCartridge(std::filesystem::path path);
EmuEvent Insert8MbitDRAMCartridge();
EmuEvent Insert32MbitDRAMCartridge();
EmuEvent Insert48MbitDRAMCartridge();
EmuEvent InsertROMCartridge(std::filesystem::path path);
EmuEvent InsertCartridgeFromSettings();

EmuEvent DeleteBackupFile(std::string filename, bool external);
EmuEvent FormatBackupMemory(bool external);

EmuEvent LoadInternalBackupMemory();

EmuEvent SetEmulateSH2Cache(bool enable);
EmuEvent SetSH2ClockFactor(uint32 factor);

EmuEvent SetCDBlockLLE(bool enable);

EmuEvent EnableThreadedVDP1(bool enable);
EmuEvent EnableThreadedVDP2(bool enable);
EmuEvent EnableThreadedDeinterlacer(bool enable);

EmuEvent EnableThreadedSCSP(bool enable);
EmuEvent SetSCSPStepGranularity(uint32 granularity);

EmuEvent LoadState(uint32 slotIndex);
EmuEvent SaveState(uint32 slotIndex);
EmuEvent UndoSaveState();
EmuEvent UndoLoadState();

} // namespace app::events::emu
