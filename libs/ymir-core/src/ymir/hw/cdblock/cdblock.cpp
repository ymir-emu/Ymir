#include <ymir/hw/cdblock/cdblock.hpp>

#include "cdblock_devlog.hpp"

#include <ymir/sys/clocks.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/dev_assert.hpp>

#include <algorithm>
#include <cassert>
#include <utility>

namespace ymir::cdblock {

// -----------------------------------------------------------------------------
// Debugger

FORCE_INLINE static void TraceReset(debug::ICDBlockTracer *tracer) {
    if (tracer) {
        return tracer->Reset();
    }
}

FORCE_INLINE static void TraceProcessCommand(debug::ICDBlockTracer *tracer, uint16 cr1, uint16 cr2, uint16 cr3,
                                             uint16 cr4) {
    if (tracer) {
        return tracer->ProcessCommand(cr1, cr2, cr3, cr4);
    }
}

FORCE_INLINE static void TraceProcessCommandResponse(debug::ICDBlockTracer *tracer, uint16 cr1, uint16 cr2, uint16 cr3,
                                                     uint16 cr4) {
    if (tracer) {
        return tracer->ProcessCommandResponse(cr1, cr2, cr3, cr4);
    }
}

// -----------------------------------------------------------------------------
// Utility

static uint32 CalcPutOffset(uint32 size) {
    return std::min(2352u - size, 24u);
}

// -----------------------------------------------------------------------------
// Implementation

// NOTE: cannot be less than 2 due to how the Seek state processing is implemented
// - Digital Dance Mix Vol. 1 - Namie Amuro -- requires at least 5 seek ticks due to issuing two Play commands in a
//   short interval. The first Play command should not have enough time to read the disc.
static constexpr uint32 kSeekTicks = 5;

CDBlock::CDBlock(core::Scheduler &scheduler, media::CDInterface &cdif, const media::fs::Filesystem &fs,
                 core::Configuration::CDBlock &config)
    : m_scheduler(scheduler)
    , m_cdif(cdif)
    , m_fs(fs) {

    m_driveStateUpdateEvent = m_scheduler.RegisterEvent(core::events::CDBlockDriveState, this, OnDriveStateUpdateEvent);
    m_commandExecEvent = m_scheduler.RegisterEvent(core::events::CDBlockCommand, this, OnCommandExecEvent);

    config.readSpeedFactor.Observe([&](uint8 factor) {
        m_readSpeedFactor = std::clamp<uint8>(factor, 2u, 200u);
        if (m_readSpeed > 1) {
            m_readSpeed = m_readSpeedFactor;
            devlog::info<grp::base>("Read speed set to {}x", m_readSpeed);
        }
    });
    m_readSpeedFactor = 2;

    config.useLLE.ObserveAndNotify([&](bool useLLE) {
        m_eventsEnabled = !useLLE;
        if (!m_eventsEnabled) {
            m_scheduler.Cancel(m_driveStateUpdateEvent);
            m_scheduler.Cancel(m_commandExecEvent);
        }
    });

    for (int i = 0; auto &filter : m_filters) {
        filter.index = i;
        i++;
    }

    Reset(true);
}

void CDBlock::Reset(bool hard) {
    m_CR.fill(0);

    m_RR[0] = 0x0043; // ' C'
    m_RR[1] = 0x4442; // 'DB'
    m_RR[2] = 0x4C4F; // 'LO'
    m_RR[3] = 0x434B; // 'CK'

    m_readyForPeriodicReports = false;

    m_status.statusCode = kStatusCodePause;
    m_status.frameAddress = 0xFFFFFF;
    m_status.flags = 0xF;
    m_status.repeatCount = 0xF;
    m_status.controlADR = 0xFF;
    m_status.track = 0xFF;
    m_status.index = 0xFF;

    m_currDriveCycles = 0;
    m_targetDriveCycles = kDriveCyclesNotPlaying;
    if (m_eventsEnabled) {
        m_scheduler.ScheduleFromNow(m_driveStateUpdateEvent, 0);
    }

    m_playStartParam = 0;
    m_playEndParam = 0;
    m_playRepeatParam = 0;
    m_scanDirection = false;
    m_scanCounter = 0;

    m_playStartPos = 0;
    m_playEndPos = 0;
    m_playMaxRepeat = 0;
    m_playFile = false;
    m_bufferFullPause = false;
    m_playEndPending = false;

    m_readSpeed = 1;

    m_discAuthStatus = 0;
    m_mpegAuthStatus = 0;

    if (hard) {
        m_HIRQ = 0x0BE1;
        m_HIRQMASK = 0;
    } else {
        m_HIRQ &= kHIRQ_mask & ~kHIRQ_DCHG;
    }

    m_xferType = TransferType::None;
    m_xferPos = 0;
    m_xferLength = 0;
    m_xferCount = 0xFFFFFF;
    m_xferBuffer.fill(0xFFFF);
    m_xferBufferPos = 0;

    m_xferSubcodeFrameAddress = 0;
    m_xferSubcodeGroup = 0;

    m_partitionManager.Reset();

    for (auto &filter : m_filters) {
        filter.Reset();
    }
    m_cdDeviceConnection = Filter::kDisconnected;
    m_lastCDWritePartition = 0xFF;

    m_calculatedPartitionSize = 0;

    m_getSectorLength = 2048;
    m_putSectorLength = 2048;
    m_putOffset = 0;

    m_processingCommand = false;

    m_netlinkSCR = 0x00;

    TraceReset(m_tracer);
}

void CDBlock::MapMemory(sys::SH2Bus &bus) {
    // CD Block registers are mirrored every 64 bytes in a 4 KiB block.
    // These 4 KiB blocks are mapped every 32 KiB.

    static constexpr auto cast = [](void *ctx) -> CDBlock & { return *static_cast<CDBlock *>(ctx); };

    for (uint32 address = 0x580'0000; address <= 0x58F'FFFF; address += 0x8000) {
        bus.MapNormal(
            address, address + 0xFFF, this,
            [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadReg<uint8>(address); },
            [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadReg<uint16>(address); },
            [](uint32 address, void *ctx) -> uint32 {
                uint32 value = cast(ctx).ReadReg<uint16>(address + 0) << 16u;
                value |= cast(ctx).ReadReg<uint16>(address + 2) << 0u;
                return value;
            },
            [](uint32 address, uint8 value, void *ctx) { cast(ctx).WriteReg<uint8>(address, value); },
            [](uint32 address, uint16 value, void *ctx) { cast(ctx).WriteReg<uint16>(address, value); },
            [](uint32 address, uint32 value, void *ctx) {
                cast(ctx).WriteReg<uint16>(address + 0, value >> 16u);
                cast(ctx).WriteReg<uint16>(address + 2, value >> 0u);
            },
            // Bus wait handler
            [](uint32, uint32, bool, void *) -> bool { return false; });

        bus.MapSideEffectFree(
            address, address + 0xFFF, this,
            [](uint32 address, void *ctx) -> uint8 { return cast(ctx).PeekReg<uint8>(address); },
            [](uint32 address, void *ctx) -> uint16 { return cast(ctx).PeekReg<uint16>(address); },
            [](uint32 address, void *ctx) -> uint32 { return cast(ctx).PeekReg<uint32>(address); },
            [](uint32 address, uint8 value, void *ctx) { cast(ctx).PokeReg<uint8>(address, value); },
            [](uint32 address, uint16 value, void *ctx) { cast(ctx).PokeReg<uint16>(address, value); },
            [](uint32 address, uint32 value, void *ctx) { cast(ctx).PokeReg<uint32>(address, value); });
    }
}

void CDBlock::UpdateClockRatios(const sys::ClockRatios &clockRatios) {
    // Drive state updates is counted in thirds, as explained in cdblock_defs.hpp
    m_scheduler.SetEventCountFactor(m_driveStateUpdateEvent, clockRatios.CDBlockNum * 3, clockRatios.CDBlockDen);
    m_scheduler.SetEventCountFactor(m_commandExecEvent, clockRatios.CDBlockNum, clockRatios.CDBlockDen);
}

void CDBlock::OnDiscLoaded() {
    const uint8 status = GetStatusCode();
    if (status == kStatusCodeNoDisc || status == kStatusCodeOpen || status == kStatusCodePlay ||
        status == kStatusCodeSeek || status == kStatusCodeBusy) {
        // TODO: stay in Busy status while disc is read
        m_status.statusCode = kStatusCodePause;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_discAuthStatus = 0;
    }
    SetInterrupt(kHIRQ_DCHG | kHIRQ_EFLS);
}

void CDBlock::OnDiscEjected() {
    m_status.frameAddress = 0xFFFFFF;
    m_status.flags = 0xF;
    m_status.repeatCount = 0xF;
    m_status.controlADR = 0xFF;
    m_status.track = 0xFF;
    m_status.index = 0xFF;

    m_fsState.Reset();
    OpenTray();

    devlog::debug<grp::base>("Ejected disc");
}

void CDBlock::OpenTray() {
    if (GetStatusCode() != kStatusCodeOpen) {
        // TODO: stay in Busy status while disc stops spinning
        m_status.statusCode = kStatusCodeOpen;
        m_discAuthStatus = 0;
        m_targetDriveCycles = kDriveCyclesNotPlaying;

        SetInterrupt(kHIRQ_DCHG | kHIRQ_EFLS);

        devlog::info<grp::base>("Tray opened");
    } else {
        devlog::info<grp::base>("Tried to open tray when it's already opened");
    }
}

void CDBlock::CloseTray() {
    if (GetStatusCode() == kStatusCodeOpen) {
        // TODO: stay in Busy status while drive scans disc
        if (m_cdif.HasDisc()) {
            m_status.statusCode = kStatusCodePause;
            m_targetDriveCycles = kDriveCyclesNotPlaying;

            devlog::info<grp::base>("Tray closed - paused");
        } else {
            m_status.statusCode = kStatusCodeNoDisc;
            m_targetDriveCycles = kDriveCyclesNotPlaying;

            devlog::info<grp::base>("Tray closed - no disc");
        }
    } else {
        devlog::info<grp::base>("Tried to close tray when it's already closed");
    }
}

bool CDBlock::IsTrayOpen() const {
    return GetStatusCode() == kStatusCodeOpen;
}

void CDBlock::SaveState(savestate::CDBlockSaveState &state) const {
    state.CR = m_CR;
    state.RR = m_RR;
    state.HIRQ = m_HIRQ;
    state.HIRQMASK = m_HIRQMASK;

    state.status.statusCode = m_status.statusCode;
    state.status.frameAddress = m_status.frameAddress;
    state.status.flags = m_status.flags;
    state.status.repeatCount = m_status.repeatCount;
    state.status.controlADR = m_status.controlADR;
    state.status.track = m_status.track;
    state.status.index = m_status.index;

    state.readyForPeriodicReports = m_readyForPeriodicReports;

    state.currDriveCycles = m_currDriveCycles;
    state.targetDriveCycles = m_targetDriveCycles;
    state.seekTicks = m_seekTicks;

    state.playStartParam = m_playStartParam;
    state.playEndParam = m_playEndParam;
    state.playRepeatParam = m_playRepeatParam;
    state.scanDirection = m_scanDirection;
    state.scanCounter = m_scanCounter;

    state.playStartPos = m_playStartPos;
    state.playEndPos = m_playEndPos;
    state.playMaxRepeat = m_playMaxRepeat;
    state.playFile = m_playFile;
    state.bufferFullPause = m_bufferFullPause;
    state.playEndPending = m_playEndPending;

    state.readSpeed = m_readSpeed;

    state.discAuthStatus = m_discAuthStatus;

    state.mpegAuthStatus = m_mpegAuthStatus;

    switch (m_xferType) {
    case TransferType::None: state.xferType = savestate::CDBlockSaveState::TransferType::None; break;
    case TransferType::TOC: state.xferType = savestate::CDBlockSaveState::TransferType::TOC; break;
    case TransferType::GetSector: state.xferType = savestate::CDBlockSaveState::TransferType::GetSector; break;
    case TransferType::GetThenDeleteSector:
        state.xferType = savestate::CDBlockSaveState::TransferType::GetThenDeleteSector;
        break;
    case TransferType::PutSector: state.xferType = savestate::CDBlockSaveState::TransferType::PutSector; break;
    case TransferType::FileInfo: state.xferType = savestate::CDBlockSaveState::TransferType::FileInfo; break;
    case TransferType::Subcode: state.xferType = savestate::CDBlockSaveState::TransferType::Subcode; break;
    }

    state.xferPos = m_xferPos;
    state.xferLength = m_xferLength;
    state.xferCount = m_xferCount;
    state.xferBuffer = m_xferBuffer;
    state.xferBufferPos = m_xferBufferPos;

    state.xferSectorPos = m_xferSectorPos;
    state.xferSectorEnd = m_xferSectorEnd;
    state.xferPartition = m_xferPartition;
    state.xferGetLength = m_xferGetLength;
    state.xferDelCount = m_xferDelCount;

    state.xferSubcodeFrameAddress = m_xferSubcodeFrameAddress;
    state.xferSubcodeGroup = m_xferSubcodeGroup;

    state.xferExtraCount = m_xferExtraCount;

    // Store partition buffers first, then scratch buffers.
    // Since there are always 200 buffers (and one extra scratch buffer), we can use the free buffer count to determine
    // both how many scratch buffers to write and where to begin writing them.

    // Clear all buffers first
    for (auto &buffer : state.buffers) {
        buffer.data.fill(0);
        buffer.size = 0;
        buffer.frameAddress = 0;
        buffer.fileNum = 0;
        buffer.chanNum = 0;
        buffer.submode = 0;
        buffer.codingInfo = 0;
        buffer.partitionIndex = 0xFF;
    }

    // Write partition buffers
    m_partitionManager.SaveState(state);

    // Write scratch buffers after partition buffers
    const uint32 basePos = kNumBuffers - m_partitionManager.GetFreeBufferCount();
    uint32 pos = basePos;
    while (pos < kNumBuffers + 1) {
        auto &scratchBuffer = m_scratchBuffers[pos - basePos];
        state.buffers[pos].data = scratchBuffer.data;
        state.buffers[pos].size = scratchBuffer.size;
        state.buffers[pos].frameAddress = scratchBuffer.frameAddress;
        state.buffers[pos].fileNum = scratchBuffer.subheader.fileNum;
        state.buffers[pos].chanNum = scratchBuffer.subheader.chanNum;
        state.buffers[pos].submode = scratchBuffer.subheader.submode;
        state.buffers[pos].codingInfo = scratchBuffer.subheader.codingInfo;
        ++pos;
    }

    state.scratchBufferPutIndex = m_scratchBufferPutIndex;

    for (size_t i = 0; i < kNumFilters; i++) {
        state.filters[i].startFrameAddress = m_filters[i].startFrameAddress;
        state.filters[i].frameAddressCount = m_filters[i].frameAddressCount;

        state.filters[i].mode = m_filters[i].mode;

        state.filters[i].fileNum = m_filters[i].fileNum;
        state.filters[i].chanNum = m_filters[i].chanNum;

        state.filters[i].submodeMask = m_filters[i].submodeMask;
        state.filters[i].submodeValue = m_filters[i].submodeValue;

        state.filters[i].codingInfoMask = m_filters[i].codingInfoMask;
        state.filters[i].codingInfoValue = m_filters[i].codingInfoValue;

        state.filters[i].passOutput = m_filters[i].passOutput;
        state.filters[i].failOutput = m_filters[i].failOutput;
    }

    state.cdDeviceConnection = m_cdDeviceConnection;
    state.lastCDWritePartition = m_lastCDWritePartition;

    state.calculatedPartitionSize = m_calculatedPartitionSize;

    state.getSectorLength = m_getSectorLength;
    state.putSectorLength = m_putSectorLength;

    state.processingCommand = m_processingCommand;

    m_fsState.SaveState(state.fs);
}

bool CDBlock::ValidateState(const savestate::CDBlockSaveState &state) const {
    if (!m_partitionManager.ValidateState(state)) {
        return false;
    }
    if (!m_fsState.ValidateState(state.fs)) {
        return false;
    }
    return true;
}

void CDBlock::LoadState(const savestate::CDBlockSaveState &state) {
    m_CR = state.CR;
    m_RR = state.RR;
    m_HIRQ = state.HIRQ;
    m_HIRQMASK = state.HIRQMASK;

    m_status.statusCode = state.status.statusCode;
    m_status.frameAddress = state.status.frameAddress;
    m_status.flags = state.status.flags;
    m_status.repeatCount = state.status.repeatCount;
    m_status.controlADR = state.status.controlADR;
    m_status.track = state.status.track;
    m_status.index = state.status.index;

    m_readyForPeriodicReports = state.readyForPeriodicReports;

    m_currDriveCycles = state.currDriveCycles;
    m_targetDriveCycles = state.targetDriveCycles;
    m_seekTicks = state.seekTicks;

    m_playStartParam = state.playStartParam;
    m_playEndParam = state.playEndParam;
    m_playRepeatParam = state.playRepeatParam;
    m_scanDirection = state.scanDirection;
    m_scanCounter = state.scanCounter;

    m_playStartPos = state.playStartPos;
    m_playEndPos = state.playEndPos;
    m_playMaxRepeat = state.playMaxRepeat;
    m_playFile = state.playFile;
    m_bufferFullPause = state.bufferFullPause;
    m_playEndPending = state.playEndPending;

    m_readSpeed = state.readSpeed;

    m_discAuthStatus = state.discAuthStatus;

    m_mpegAuthStatus = state.mpegAuthStatus;

    switch (state.xferType) {
    default: [[fallthrough]];
    case savestate::CDBlockSaveState::TransferType::None: m_xferType = TransferType::None; break;
    case savestate::CDBlockSaveState::TransferType::TOC: m_xferType = TransferType::TOC; break;
    case savestate::CDBlockSaveState::TransferType::GetSector: m_xferType = TransferType::GetSector; break;
    case savestate::CDBlockSaveState::TransferType::GetThenDeleteSector:
        m_xferType = TransferType::GetThenDeleteSector;
        break;
    case savestate::CDBlockSaveState::TransferType::PutSector: m_xferType = TransferType::PutSector; break;
    case savestate::CDBlockSaveState::TransferType::FileInfo: m_xferType = TransferType::FileInfo; break;
    case savestate::CDBlockSaveState::TransferType::Subcode: m_xferType = TransferType::Subcode; break;
    }

    m_xferPos = state.xferPos;
    m_xferLength = state.xferLength;
    m_xferCount = state.xferCount;
    m_xferBuffer = state.xferBuffer;
    m_xferBufferPos = state.xferBufferPos;

    m_xferSectorPos = state.xferSectorPos;
    m_xferSectorEnd = state.xferSectorEnd;
    m_xferPartition = state.xferPartition;
    m_xferGetLength = state.xferGetLength;
    m_xferDelCount = state.xferDelCount;

    m_xferSubcodeFrameAddress = state.xferSubcodeFrameAddress;
    m_xferSubcodeGroup = state.xferSubcodeGroup;

    m_xferExtraCount = state.xferExtraCount;

    // Read partition buffers followed by scratch buffers
    m_partitionManager.LoadState(state);
    const uint32 basePos = kNumBuffers - m_partitionManager.GetFreeBufferCount();
    uint32 pos = basePos;
    while (pos < kNumBuffers + 1) {
        auto &scratchBuffer = m_scratchBuffers[pos - basePos];
        scratchBuffer.data = state.buffers[pos].data;
        scratchBuffer.size = state.buffers[pos].size;
        scratchBuffer.frameAddress = state.buffers[pos].frameAddress;
        scratchBuffer.subheader.fileNum = state.buffers[pos].fileNum;
        scratchBuffer.subheader.chanNum = state.buffers[pos].chanNum;
        scratchBuffer.subheader.submode = state.buffers[pos].submode;
        scratchBuffer.subheader.codingInfo = state.buffers[pos].codingInfo;
        ++pos;
    }

    m_scratchBufferPutIndex = state.scratchBufferPutIndex;

    for (size_t i = 0; i < kNumFilters; i++) {
        m_filters[i].startFrameAddress = state.filters[i].startFrameAddress;
        m_filters[i].frameAddressCount = state.filters[i].frameAddressCount;

        m_filters[i].mode = state.filters[i].mode;

        m_filters[i].fileNum = state.filters[i].fileNum;
        m_filters[i].chanNum = state.filters[i].chanNum;

        m_filters[i].submodeMask = state.filters[i].submodeMask;
        m_filters[i].submodeValue = state.filters[i].submodeValue;

        m_filters[i].codingInfoMask = state.filters[i].codingInfoMask;
        m_filters[i].codingInfoValue = state.filters[i].codingInfoValue;

        m_filters[i].passOutput = state.filters[i].passOutput;
        m_filters[i].failOutput = state.filters[i].failOutput;
    }

    m_cdDeviceConnection = state.cdDeviceConnection;
    m_lastCDWritePartition = state.lastCDWritePartition;

    m_calculatedPartitionSize = state.calculatedPartitionSize;

    m_getSectorLength = state.getSectorLength;
    m_putSectorLength = state.putSectorLength;
    m_putOffset = CalcPutOffset(m_putSectorLength);

    m_processingCommand = state.processingCommand;

    m_fsState.LoadState(state.fs);
}

void CDBlock::OnDriveStateUpdateEvent(core::EventContext &eventContext, void *userContext) {
    auto &cdb = *static_cast<CDBlock *>(userContext);
    cdb.ProcessDriveState();
    eventContext.Reschedule(cdb.m_targetDriveCycles);
}

void CDBlock::OnCommandExecEvent(core::EventContext &eventContext, void *userContext) {
    auto &cdb = *static_cast<CDBlock *>(userContext);
    cdb.ProcessCommand();
}

template <mem_primitive T>
T CDBlock::ReadReg(uint32 address) {
    if constexpr (std::is_same_v<T, uint8>) {
        if (address == 0x5895019) {
            return 0x30;
        }
        if (address == 0x589501D) {
            return m_netlinkSCR;
        }
    }

    address &= 0x3F;

    switch (address) {
    case 0x00: return DoReadTransfer();
    case 0x02: return DoReadTransfer();
    case 0x08: return m_HIRQ;
    case 0x0C: return m_HIRQMASK;
    case 0x18: return m_RR[0];
    case 0x1C: return m_RR[1];
    case 0x20: return m_RR[2];
    case 0x24:
        m_processingCommand = false;
        m_readyForPeriodicReports = true;
        return m_RR[3];
    default: devlog::debug<grp::regs>("Unhandled {}-bit register read from {:02X}", sizeof(T) * 8, address); return 0;
    }
}

template <mem_primitive T>
void CDBlock::WriteReg(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint8>) {
        if (address == 0x589501D) {
            m_netlinkSCR = value;
            return;
        }
        if (address == 0x582503D) {
            return;
        }
    }

    address &= 0x3F;

    devlog::trace<grp::regs>("{}-bit register write to {:02X} = {:X}", sizeof(T) * 8, address, value);
    switch (address) {
    case 0x00: DoWriteTransfer(value); break;
    case 0x02: DoWriteTransfer(value); break;
    case 0x08:
        m_HIRQ &= value;
        UpdateInterrupts();
        break;
    case 0x0C:
        m_HIRQMASK = value;
        UpdateInterrupts();
        break;
    case 0x18:
        m_processingCommand = true;
        m_CR[0] = value;
        break;
    case 0x1C: m_CR[1] = value; break;
    case 0x20: m_CR[2] = value; break;
    case 0x24:
        m_CR[3] = value;
        SetupCommand();
        break;

    default:
        devlog::debug<grp::regs>("Unhandled {}-bit register write to {:02X} = {:X}", sizeof(T) * 8, address, value);
        break;
    }
}

template <mem_primitive T>
T CDBlock::PeekReg(uint32 address) {
    if constexpr (std::is_same_v<T, uint8>) {
        return PeekReg<uint16>(address & ~1) >> ((~address & 1u) * 8u);
    } else if constexpr (std::is_same_v<T, uint32>) {
        uint32 value = PeekReg<uint16>((address & ~1) | 0) << 16u;
        value |= PeekReg<uint16>((address & ~1) | 2) << 0u;
        return value;
    } else if constexpr (std::is_same_v<T, uint16>) {
        address &= 0x3F;

        // NOTE: CR and RR are exposed separately and simultaneously for debugging purposes.
        // ReadReg and WriteReg implement the correct register set.

        switch (address) {
        case 0x00: return m_xferBuffer[m_xferBufferPos % m_xferBuffer.size()];
        case 0x02: return m_xferBuffer[m_xferBufferPos % m_xferBuffer.size()];

        case 0x08: return m_HIRQ;
        case 0x0C: return m_HIRQMASK;

        case 0x18: return m_CR[0];
        case 0x1C: return m_CR[1];
        case 0x20: return m_CR[2];
        case 0x24: return m_CR[3];

        case 0x28: return m_RR[0];
        case 0x2C: return m_RR[1];
        case 0x30: return m_RR[2];
        case 0x34: return m_RR[3];
        default: return 0;
        }
    }
    util::unreachable();
}

template <mem_primitive T>
void CDBlock::PokeReg(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint8>) {
        PokeReg<uint16>(address & ~1, value << ((~address & 1) * 8));
    } else if constexpr (std::is_same_v<T, uint32>) {
        PokeReg<uint16>((address & ~1) | 0, value >> 16u);
        PokeReg<uint16>((address & ~1) | 2, value >> 0u);
    } else if constexpr (std::is_same_v<T, uint16>) {
        address &= 0x3F;

        // NOTE: CR and RR are exposed separately and simultaneously for debugging purposes.
        // ReadReg and WriteReg implement the correct register set.

        switch (address) {
        case 0x00: m_xferBuffer[m_xferBufferPos % m_xferBuffer.size()] = value; break;
        case 0x02: m_xferBuffer[m_xferBufferPos % m_xferBuffer.size()] = value; break;

        case 0x08: m_HIRQ = value; break;
        case 0x0C: m_HIRQMASK = value; break;

        case 0x18: m_CR[0] = value; break;
        case 0x1C: m_CR[1] = value; break;
        case 0x20: m_CR[2] = value; break;
        case 0x24: m_CR[3] = value; break;

        case 0x28: m_RR[0] = value; break;
        case 0x2C: m_RR[1] = value; break;
        case 0x30: m_RR[2] = value; break;
        case 0x34: m_RR[3] = value; break;
        }
    }
}

bool CDBlock::SetupGenericPlayback(uint32 startParam, uint32 endParam, uint16 repeatParam) {
    // Make sure we have a disc
    if (!m_cdif.HasDisc()) {
        devlog::info<grp::play_init>("No disc");
        m_status.statusCode = kStatusCodeNoDisc;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        return true;
    }

    const bool keepStartParam = startParam == 0xFFFFFF;
    const bool keepEndParam = endParam == 0xFFFFFF;
    const bool keepRepeatParam = repeatParam == 0xFF;

    const bool isStartFAD = bit::test<23>(keepStartParam ? m_playStartParam : startParam);
    const bool isEndFAD = bit::test<23>(keepEndParam ? m_playEndParam : endParam);

    const bool paused = GetStatusCode() == kStatusCodePause;

    // Handle resume from pause for data tracks
    if (keepStartParam && keepEndParam && keepRepeatParam && isStartFAD && isEndFAD && paused) {
        m_status.statusCode = kStatusCodePlay;
        m_targetDriveCycles = kDriveCyclesPlaying1x / m_readSpeed;
        devlog::debug<grp::play_init>("Resuming from pause");
        return true;
    }

    // Handle "no change" parameters
    if (keepStartParam) {
        startParam = m_playStartParam;
    }
    if (keepEndParam) {
        endParam = m_playEndParam;
    }
    if (keepRepeatParam) {
        repeatParam = m_playRepeatParam;
    }

    const bool resetPos = !keepRepeatParam && !bit::test<7>(repeatParam);

    // Sanity check: both must be FADs or tracks, not a mix
    if (isStartFAD != isEndFAD) {
        devlog::debug<grp::play_init>("Start/End FAD type mismatch: {:06X} {:06X}", startParam, endParam);
        return false; // reject
    }

    // Store playback parameters
    m_playStartParam = startParam;
    m_playEndParam = endParam;
    m_playRepeatParam = repeatParam;
    m_playMaxRepeat = m_playRepeatParam & 0xF;
    m_playFile = false;
    m_playEndPending = false;

    const media::TOC &toc = m_cdif.GetTOC();

    // TODO: deduplicate code

    if (isStartFAD) {
        // Frame address range
        m_playStartPos = startParam & 0x7FFFFF;
        if (!keepEndParam) {
            m_playEndPos = m_playStartPos + (endParam & 0x7FFFFF) - 1;
        }

        devlog::debug<grp::play_init>("FAD range {:06X} to {:06X}", m_playStartPos, m_playEndPos);

        uint32 frameAddress = m_status.frameAddress;
        if (resetPos) {
            frameAddress = m_playStartPos;
            devlog::debug<grp::play_init>("Reset playback position to {:06X}", frameAddress);
        } else if (frameAddress < m_playStartPos || frameAddress > m_playEndPos + 1) {
            devlog::debug<grp::play_init>(
                "Adjusting playback position from {:06X} to {:06X} to fit range {:06X}..{:06X}", frameAddress,
                m_playStartPos, m_playStartPos, m_playEndPos);
            frameAddress = m_playStartPos;
        } else {
            devlog::debug<grp::play_init>("Continuing playback from {:06X}", frameAddress);
        }

        // Seek to frame address
        const media::TrackInfo *trackInfo = toc.GetTrackInfoForFAD(frameAddress);
        if (trackInfo != nullptr) [[likely]] {
            m_cdif.BeginSeekToFrameAddress(frameAddress);
            m_status.statusCode = kStatusCodeSeek;
            m_status.frameAddress = frameAddress;
            m_status.flags = trackInfo->controlADR == 0x01 ? 0x8 : 0x0;
            m_status.repeatCount = 0; // first repeat
            m_status.controlADR = trackInfo->controlADR;
            m_status.track = trackInfo->number;
            m_status.index = 1;
            m_seekTicks = kSeekTicks; // TODO: calculate realistic seek time

            if (m_status.controlADR == 0x41) {
                m_targetDriveCycles = kDriveCyclesPlaying1x / m_readSpeed;
            } else {
                // Force 1x speed if playing audio track
                m_targetDriveCycles = kDriveCyclesPlaying1x;
            }

            devlog::debug<grp::play_init>("Track:Index {:02d}:{:02d} ctl/ADR={:02X}", m_status.track, m_status.index,
                                          m_status.controlADR);
        } else {
            devlog::debug<grp::play_init>("Could not find track at FAD {:06X}; pausing", frameAddress);
            m_targetDriveCycles = kDriveCyclesNotPlaying;
            m_status.statusCode = kStatusCodePause;
        }
    } else {
        // Track range

        // startParam and endParam contain the track number on the upper byte and index on the lower byte
        uint8 startTrack = bit::extract<8, 15>(startParam);
        uint8 startIndex = bit::extract<0, 7>(startParam);
        uint8 endTrack = bit::extract<8, 15>(endParam);
        uint8 endIndex = bit::extract<0, 7>(endParam);

        // Handle default parameters - use first or last track and index in the disc
        if (startParam == 0) {
            startTrack = toc.GetFirstTrackNumber();
            startIndex = 1;
        }
        if (endParam == 0) {
            endTrack = toc.GetLastTrackNumber();
            endIndex = 99; // TODO: is this correct?
        }

        devlog::debug<grp::play_init>("Track:Index range {:02d}:{:02d}-{:02d}:{:02d} ", startTrack, startIndex,
                                      endTrack, endIndex);

        // Clamp track numbers to what's available in the disc
        // If end < start, ProcessDriveState() will switch to the Pause state automatically
        uint8 firstTrack = toc.GetFirstTrackNumber();
        uint8 lastTrack = toc.GetLastTrackNumber();
        startTrack = std::clamp(startTrack, firstTrack, lastTrack);
        endTrack = std::clamp(endTrack, firstTrack, lastTrack);
        startIndex = std::clamp<uint8>(startIndex, 1, 99);
        endIndex = std::clamp<uint8>(endIndex, 1, 99);
        devlog::debug<grp::play_init>("Track:Index range after clamping {:02d}:{:02d}-{:02d}:{:02d}", startTrack,
                                      startIndex, endTrack, endIndex);

        // Play frame address range for the specified tracks
        m_playStartPos = toc.GetStartFADForTrack(startTrack);
        m_playEndPos = toc.GetEndFADForTrack(endTrack);

        uint32 frameAddress = m_status.frameAddress;
        if (resetPos) {
            m_cdif.BeginSeekToTrackIndex(startTrack, startIndex);
            frameAddress = m_playStartPos;
            devlog::debug<grp::play_init>("Reset playback position to {:06X}", frameAddress);
        } else if (frameAddress < m_playStartPos || frameAddress > m_playEndPos + 1) {
            m_cdif.BeginSeekToTrackIndex(startTrack, startIndex);
            devlog::debug<grp::play_init>(
                "Adjusting playback position from {:06X} to {:06X} to fit range {:06X}..{:06X}", frameAddress,
                m_playStartPos, m_playStartPos, m_playEndPos);
            frameAddress = m_playStartPos;
        } else {
            m_cdif.BeginSeekToFrameAddress(frameAddress);
            devlog::debug<grp::play_init>("Continuing playback from {:06X}", frameAddress);
        }

        const media::TrackInfo *trackInfo = toc.GetTrackInfoForFAD(frameAddress);
        if (trackInfo != nullptr) [[likely]] {
            m_status.statusCode = kStatusCodeSeek;
            m_status.flags = trackInfo->controlADR == 0x01 ? 0x8 : 0x0;
            m_status.repeatCount = 0; // first repeat
            m_status.controlADR = trackInfo->controlADR;
            m_status.track = trackInfo->number;
            m_status.index = 1;
            m_seekTicks = kSeekTicks; // TODO: calculate realistic seek time

            if (m_status.controlADR == 0x41) {
                m_targetDriveCycles = kDriveCyclesPlaying1x / m_readSpeed;
            } else {
                // Force 1x speed if playing audio track
                m_targetDriveCycles = kDriveCyclesPlaying1x;
            }

            devlog::debug<grp::play_init>("Track:Index {:02d}:{:02d} ctl/ADR={:02X}", m_status.track, m_status.index,
                                          m_status.controlADR);
        } else {
            // Handle as a disc read error
            // TODO: what happens on a real disc read error?
            devlog::debug<grp::play>("Could not find track - disc was removed, is damaged or corrupted");
            return false;
        }
    }

    return true;
}

bool CDBlock::SetupFilePlayback(uint32 fileID, uint32 offset, uint8 filterNumber) {
    // Bail out if there is no file system
    if (!m_fs.IsValid()) {
        devlog::debug<grp::play_init>("No file system; rejecting playback request");
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_status.statusCode = kStatusCodePause;
        return true;
    }

    // Bail out if there is no current directory
    if (!m_fsState.HasCurrentDirectory()) {
        devlog::debug<grp::play_init>("No current directory set; rejecting playback request");
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_status.statusCode = kStatusCodePause;
        return true;
    }

    // Reject if the file ID is out of range
    const media::fs::FileInfo &fileInfo = m_fsState.GetFileInfo(fileID);
    if (!fileInfo.IsValid()) {
        devlog::debug<grp::play_init>("Invalid file ID {:X}; rejecting playback request", fileID);
        return false;
    }

    // Reject if file ID points to a directory
    // TODO: should this be rejected?
    if (fileInfo.IsDirectory()) {
        devlog::warn<grp::play_init>("Attempting to read a directory (file ID {:X}); rejecting playback request",
                                     fileID);
        return false;
    }

    // Reject if frame address doesn't point to a valid data track
    const uint32 fileOffset = fileInfo.frameAddress + offset;
    const media::TrackInfo *info = m_cdif.GetTOC().GetTrackInfoForFAD(fileOffset);
    if (info == nullptr) {
        devlog::debug<grp::play_init>("Track not found for frame address {:06X}; rejecting playback request",
                                      fileOffset);
        return false;
    }
    if (info->controlADR != 0x41) {
        devlog::debug<grp::play_init>("Not a data track at frame address {:06X}; rejecting playback request",
                                      fileOffset);
        return false;
    }

    // Determine starting and ending frame addresses and other parameters for "playback"
    m_playStartPos = fileInfo.frameAddress + offset;
    m_playEndPos = fileInfo.frameAddress + (fileInfo.fileSize + 2047) / 2048 - offset - 1;
    m_playMaxRepeat = 0;
    m_playFile = true;

    // Connect CD device to specified filter
    ConnectCDDevice(filterNumber);

    // Setup status
    m_cdif.BeginSeekToFrameAddress(m_playStartPos);
    m_status.statusCode = kStatusCodeSeek;
    m_status.flags = info->controlADR == 0x41 ? 0x8 : 0x0;
    m_status.repeatCount = 0; // first repeat
    m_status.controlADR = info->controlADR;
    m_status.track = info->number;
    m_status.index = 1;
    m_seekTicks = kSeekTicks; // TODO: calculate realistic seek time

    devlog::debug<grp::play_init>("Read file {} (ID {}), offset {}, filter {}, frame addresses {:06X} to {:06X}",
                                  fileInfo.name, fileID, offset, filterNumber, m_playStartPos, m_playEndPos);
    return true;
}

bool CDBlock::SetupScan(uint8 direction) {
    if (direction >= 2) {
        return false;
    }

    m_scanDirection = direction;
    m_scanCounter = 0;

    m_status.statusCode = kStatusCodeScan;

    devlog::info<grp::base>("Scan disc {}", (m_scanDirection ? "backward" : "forward"));

    return true;
}

void CDBlock::ProcessDriveState() {
    CheckPlayEnd();

    // Resume playback if paused due to running out of buffers
    if (m_bufferFullPause && m_partitionManager.GetFreeBufferCount() > 0) {
        m_bufferFullPause = false;
    }

    switch (GetStatusCode()) {
    case kStatusCodeSeek:
        // HACK: Extremely hacky way to make the status transition from Seek to Play
        if (m_seekTicks > 0) {
            --m_seekTicks;
        }
        if (m_seekTicks == 1) {
            if (m_status.controlADR == 0x41) {
                m_targetDriveCycles = kDriveCyclesPlaying1x / m_readSpeed;
            } else {
                // Force 1x speed if playing audio track
                m_targetDriveCycles = kDriveCyclesPlaying1x;
            }
            if (m_cdif.IsSeekDone()) {
                m_bufferFullPause = false;
                m_status.frameAddress = m_cdif.GetSeekFrameAddress();
                if (m_status.frameAddress == 0xFFFFFF) {
                    m_status.statusCode = kStatusCodeStandby;
                    m_status.flags = 0xF;
                    m_status.repeatCount = 0xF;
                    m_status.controlADR = 0xFF;
                    m_status.track = 0xFF;
                    m_status.index = 0xFF;
                    m_targetDriveCycles = kDriveCyclesNotPlaying;
                } else {
                    ProcessDriveStatePlay();
                }
            }
        } else if (m_seekTicks == 0 && m_cdif.IsSeekDone()) {
            m_bufferFullPause = false;
            m_status.statusCode = kStatusCodePlay;
            ProcessDriveStatePlay();
        }
        break;
    case kStatusCodePlay: [[fallthrough]];
    case kStatusCodeScan: ProcessDriveStatePlay(); break;
    }

    // FIXME: cdbtest fails if the PEND interrupt happens at the same time as the CSCT interrupt from Play
    // The following games break if this line is disabled:
    // - DJ Wars -- Boots back to BIOS
    // - Gremlin Interactive Demo Disc -- Hardcore 4x4 demo stops before going "in-game"
    // - X-Men: Children of the Atom (EU) -- hangs on a black screen after certain transitions (e.g. title to menus)
    CheckPlayEnd();

    if (m_readyForPeriodicReports && !m_processingCommand) {
        // HACK to ensure the system detects the absence of a disc properly
        m_cdif.PollDriveState();
        if (!m_cdif.HasDisc() && GetStatusCode() != kStatusCodeOpen) {
            m_status.statusCode = kStatusCodeNoDisc;
            m_targetDriveCycles = kDriveCyclesNotPlaying;
        }
        if (m_playEndPending) {
            ReportCDStatus(kStatusCodeBusy | kStatusFlagPeriodic);
        } else {
            ReportCDStatus(m_status.statusCode | kStatusFlagPeriodic);
        }
        SetInterrupt(kHIRQ_SCDQ);
    }
}

void CDBlock::ProcessDriveStatePlay() {
    const bool scan = GetStatusCode() == kStatusCodeScan;
    const uint32 frameAddress = m_status.frameAddress;
    if (!m_cdif.HasDisc()) [[unlikely]] {
        devlog::debug<grp::play>("Disc removed");

        m_status.statusCode = kStatusCodeNoDisc; // TODO: is this correct?
        SetInterrupt(kHIRQ_DCHG);
    } else if (frameAddress <= m_playEndPos) {
        devlog::trace<grp::play>("Read from frame address {:06X}", frameAddress);

        if (m_bufferFullPause) {
            devlog::trace<grp::play>("Can't play disc, no buffers available");
            return;
        }

        Buffer &buffer = m_scratchBuffers[0];
        media::DiscPosition discPos{};

        // Sanity check: is the track valid?
        if (m_cdif.ReadSector(frameAddress, buffer.data, &discPos)) [[likely]] {
            devlog::trace<grp::play>("Read sector from frame address {:06X}", frameAddress);

            if (discPos.controlADR == 0x01) {
                // If playing an audio track, send to SCSP
                if (scan) {
                    // While scanning, attenuate volume by 12 dB
                    for (uint32 offset = 0; offset < 2352; offset += 2) {
                        util::WriteNE<sint16>(&buffer.data[offset], util::ReadNE<sint16>(&buffer.data[offset]) >> 2u);
                    }
                }

                // The callback returns how many thirds of the buffer are full
                const uint32 currBufferLength =
                    m_cbCDDASector(std::span<uint8, 2352>(buffer.data.begin(), buffer.data.end()));

                // Adjust pace based on how full the SCSP CDDA buffer is
                if (currBufferLength < 1) {
                    // Run faster if the buffer is less than a third full
                    m_targetDriveCycles = kDriveCyclesPlaying1x - (kDriveCyclesPlaying1x >> 2);
                } else if (currBufferLength >= 2) {
                    // Run slower if the buffer is more than two-thirds full
                    m_targetDriveCycles = kDriveCyclesPlaying1x + (kDriveCyclesPlaying1x >> 2);
                } else {
                    // Normal speed otherwise
                    m_targetDriveCycles = kDriveCyclesPlaying1x;
                }

                devlog::trace<grp::play>("Sector {:06X} sent to SCSP", frameAddress);
            } else if (m_partitionManager.GetFreeBufferCount() == 0) [[unlikely]] {
                devlog::trace<grp::play>("No free buffer available");

                // TODO: what is the correct status code here?
                // TODO: there really should be a separate state machine for handling this...
                SetInterrupt(kHIRQ_BFUL);
                m_bufferFullPause = true;
            } else {
                const bool mode2 = buffer.data[0xF] == 0x02;
                const bool mode2form2 = mode2 && bit::test<5>(buffer.data[0x12]);
                buffer.size = mode2form2 ? std::max(2324u, m_getSectorLength) : m_getSectorLength;
                buffer.frameAddress = frameAddress;
                buffer.subheader.ReadFrom(buffer.data);

                // Check against CD device filter and send data to the appropriate destination
                uint8 filterNum = m_cdDeviceConnection;
                for (int i = 0; i < kNumFilters && filterNum != Filter::kDisconnected; i++) {
                    const Filter &filter = m_filters[filterNum];
                    if (filter.Test(buffer)) {
                        if (filter.passOutput == Filter::kDisconnected) [[unlikely]] {
                            devlog::trace<grp::play>("Passed filter; output disconnected - discarded");
                        } else {
                            assert(filter.passOutput < m_filters.size());
                            devlog::trace<grp::play>("Passed filter; sent to buffer partition {}", filter.passOutput);
                            m_partitionManager.InsertHead(filter.passOutput, buffer);
                            m_lastCDWritePartition = filter.passOutput;
                            SetInterrupt(kHIRQ_CSCT);
                        }
                        break;
                    } else {
                        if (filter.failOutput == Filter::kDisconnected) [[unlikely]] {
                            devlog::trace<grp::play>("Filtered out; output disconnected - discarded");
                            break;
                        } else {
                            assert(filter.failOutput < m_filters.size());
                            devlog::trace<grp::play>("Filtered out; sent to filter {}", filter.failOutput);
                            filterNum = filter.failOutput;
                        }
                    }
                }
            }

            if (!m_bufferFullPause) {
                // Skip frames while scanning
                if (scan) {
                    constexpr uint8 kScanCounter = 15;
                    constexpr uint8 kScanFrameSkip = 75;
                    static_assert(
                        kScanFrameSkip >= kScanCounter,
                        "scan frame skip includes the frame counter, so it cannot be shorter than the counter");

                    m_scanCounter++;
                    if (m_scanCounter >= kScanCounter) {
                        m_scanCounter = 0;
                        if (m_scanDirection) {
                            m_status.frameAddress -= kScanFrameSkip + kScanCounter;
                        } else {
                            m_status.frameAddress += kScanFrameSkip - kScanCounter;
                        }
                    }
                }

                ++m_status.frameAddress;
                m_status.track = util::from_bcd(discPos.track);
                m_status.index = util::from_bcd(discPos.index);
                m_status.controlADR = discPos.controlADR;
                m_status.flags = discPos.controlADR == 0x41 ? 0x8 : 0x0;
            }
        } else {
            // Handle as a disc read error
            // TODO: what happens on a real disc read error?
            devlog::debug<grp::play>("Could not read sector - disc was removed, is damaged or corrupted");
            m_status.statusCode = kStatusCodeError;
        }
    } else {
        media::DiscPosition discPos{};
        m_cdif.ReadPosition(m_status.frameAddress, discPos);
        m_status.track = util::from_bcd(discPos.track);
        m_status.index = util::from_bcd(discPos.index);
        m_status.controlADR = discPos.controlADR;
        m_status.flags = discPos.controlADR == 0x41 ? 0x8 : 0x0;
    }

    const bool useFAD = (m_playStartParam & 0x800000) != 0;
    bool endReached;
    if (useFAD) {
        endReached = m_status.frameAddress > m_playEndPos;
    } else {
        uint8 endTrackNum = bit::extract<8, 15>(m_playEndParam);
        uint8 endIndexNum = bit::extract<0, 7>(m_playEndParam);
        if (endTrackNum == 0) {
            endTrackNum = m_cdif.GetTOC().GetLastTrackNumber();
        }
        if (endIndexNum == 0) {
            endIndexNum = 99;
        }
        const uint16 endTrackIndex = (endTrackNum << 8u) | endIndexNum;
        const uint16 curTrackIndex = (m_status.track << 8u) | m_status.index;
        endReached = curTrackIndex > endTrackIndex;
    }
    if (endReached) {
        // 0x0 to 0xE = 0 to 14 repeats
        // 0xF = infinite repeats
        if (m_playMaxRepeat == 0xF || m_status.repeatCount < m_playMaxRepeat) {
            if (m_playMaxRepeat == 0xF) {
                devlog::debug<grp::play>("Playback repeat (infinite)");
            } else {
                devlog::debug<grp::play>("Playback repeat: {} of {}", m_status.repeatCount + 1, m_playMaxRepeat);
            }
            m_status.frameAddress = m_playStartPos;
            m_status.repeatCount++;
        } else {
            devlog::debug<grp::play>("Playback ended");
            m_playEndPending = true;
            m_status.statusCode = kStatusCodePause;
        }
    }
}

void CDBlock::CheckPlayEnd() {
    if (m_playEndPending) {
        m_playEndPending = false;

        m_status.frameAddress = m_playEndPos + 1;
        m_targetDriveCycles = kDriveCyclesNotPlaying;

        uint16 hirq = kHIRQ_PEND;
        if (m_playFile) {
            hirq |= kHIRQ_EFLS | kHIRQ_EHST;
        }
        SetInterrupt(hirq);
        m_bufferFullPause = false;
        devlog::debug<grp::play>("Playback end HIRQ triggered");
    }
}

void CDBlock::SetInterrupt(uint16 bits) {
    m_HIRQ |= bits;
    UpdateInterrupts();
}

void CDBlock::UpdateInterrupts() {
    devlog::trace<grp::base>("HIRQ = {:04X}  mask = {:04X}  active = {:04X}", m_HIRQ, m_HIRQMASK, m_HIRQ & m_HIRQMASK);
    if (m_HIRQ & m_HIRQMASK) {
        m_cbTriggerExternalInterrupt0();
    }
}

void CDBlock::ReportCDStatus() {
    ReportCDStatus(m_status.statusCode);
}

void CDBlock::ReportCDStatus(uint8 statusCode) {
    m_RR[0] = (statusCode << 8u) | (m_status.flags << 4u) | (m_status.repeatCount);
    m_RR[1] = (m_status.controlADR << 8u) | m_status.track;
    m_RR[2] = (m_status.index << 8u) | ((m_status.frameAddress >> 16u) & 0xFF);
    m_RR[3] = m_status.frameAddress;
}

uint8 CDBlock::GetStatusCode() const {
    if (m_playEndPending && m_status.statusCode == kStatusCodePause) {
        // HACK: Report Play status on the last sector of playback
        return kStatusCodePlay;
    } else {
        return m_status.statusCode;
    }
}

void CDBlock::SetupTOCTransfer() {
    devlog::trace<grp::xfer>("Starting TOC transfer");

    const media::TOC &toc = m_cdif.GetTOC();
    const auto &saturnTOC = toc.GetSaturnTable();

    m_xferType = TransferType::TOC;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = saturnTOC.size() * sizeof(uint32) / sizeof(uint16);
    m_xferCount = 0;
    m_xferExtraCount = 0;

    if (m_cdif.HasDisc()) {
        for (size_t i = 0; i < saturnTOC.size(); i++) {
            m_xferBuffer[i * 2 + 0] = saturnTOC[i] >> 16u;
            m_xferBuffer[i * 2 + 1] = saturnTOC[i] >> 0u;
        }
    } else {
        std::fill_n(m_xferBuffer.begin(), m_xferLength, 0xFFFF);
    }
}

CDBlock::SectorTransferResult CDBlock::SetupGetSectorTransfer(uint16 sectorPos, uint16 sectorCount,
                                                              uint8 partitionNumber, bool del) {
    if (partitionNumber >= kNumPartitions) {
        devlog::trace<grp::xfer>("{} sector transfer rejected: invalid partition {}", (del ? "Get then delete" : "Get"),
                                 partitionNumber);
        return SectorTransferResult::Reject;
    }

    if (sectorCount == 0) {
        devlog::trace<grp::xfer>("{} sector transfer rejected: requested zero sectors",
                                 (del ? "Get then delete" : "Get"));
        return SectorTransferResult::Wait;
    }

    const uint8 partitionSize = m_partitionManager.GetBufferCount(partitionNumber);
    if (partitionSize == 0) {
        devlog::trace<grp::xfer>("{} sector transfer rejected: no data in partition {}",
                                 (del ? "Get then delete" : "Get"), partitionNumber);
        return SectorTransferResult::Reject;
    }

    m_xferSectorPos = sectorPos == 0xFFFF ? partitionSize - 1 : sectorPos;
    m_xferSectorEnd = sectorCount == 0xFFFF ? partitionSize - 1 : m_xferSectorPos + sectorCount - 1;

    if (m_xferSectorPos >= partitionSize || m_xferSectorEnd >= partitionSize) {
        devlog::trace<grp::xfer>("{} sector transfer rejected: sectors out of range ({}..{} >= {})",
                                 (del ? "Get then delete" : "Get"), m_xferSectorPos, m_xferSectorEnd, partitionNumber);
        return SectorTransferResult::Wait;
    }
    if (m_xferSectorPos > m_xferSectorEnd) {
        devlog::trace<grp::xfer>("{} sector transfer rejected: sectors range reversed: {}..{}",
                                 (del ? "Get then delete" : "Get"), m_xferSectorPos, m_xferSectorEnd);
        return SectorTransferResult::Reject;
    }

    m_xferPartition = partitionNumber;

    devlog::trace<grp::xfer>("Starting sector {} transfer - sectors {} to {} into buffer partition {}",
                             (del ? "read then delete" : "read"), sectorPos, sectorPos + sectorCount - 1,
                             partitionNumber);

    const uint32 numSectors = m_xferSectorEnd - m_xferSectorPos + 1;
    m_xferType = del ? TransferType::GetThenDeleteSector : TransferType::GetSector;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = m_getSectorLength / sizeof(uint16) * numSectors;
    m_xferCount = 0;
    m_xferExtraCount = 0;
    if (del) {
        m_xferDelStart = m_xferSectorPos;
        m_xferDelCount = numSectors;
    }

    ReadSector();

    return SectorTransferResult::OK;
}

void CDBlock::SetupPutSectorTransfer(uint16 sectorCount, uint8 partitionNumber) {
    devlog::trace<grp::xfer>("Starting sector write transfer - {} sectors into buffer partition {}", sectorCount,
                             partitionNumber);

    m_xferPartition = partitionNumber;

    m_xferType = TransferType::PutSector;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = m_putSectorLength / sizeof(uint16) * sectorCount;
    m_xferCount = 0;
    m_xferExtraCount = 0;

    m_scratchBufferPutIndex = 0;

    // Prepare sectors in scratch area
    for (uint32 i = 0; i < sectorCount; ++i) {
        auto &buffer = m_scratchBuffers[i];
        buffer.frameAddress = 0;
        buffer.size = m_putSectorLength;
        buffer.subheader.fileNum = 0;
        buffer.subheader.chanNum = 0;
        buffer.subheader.submode = 0;
        buffer.subheader.codingInfo = 0;
        buffer.data.fill(0);
    }

    // Disconnect CD device if connected to the same filter
    DisconnectCDDevice(partitionNumber);
    DisconnectFilterInput(m_xferPartition);
}

uint32 CDBlock::SetupFileInfoTransfer(uint32 fileID) {
    devlog::debug<grp::xfer>("Starting file info transfer - file ID {:X}", fileID);

    const uint32 fileOffset = m_fsState.GetFileOffset();
    const uint32 fileCount = m_fsState.GetFileCount();
    assert(fileOffset < fileCount);

    const bool readAll = fileID == 0xFFFFFF;
    uint32 numFileInfos;
    if (readAll) {
        numFileInfos = std::min(fileCount - fileOffset - 2, 254u);
    } else {
        numFileInfos = 1;
    }

    m_xferType = TransferType::FileInfo;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = numFileInfos * 12 / sizeof(uint16);
    m_xferCount = 0;
    m_xferExtraCount = 0;

    const uint32 baseFileID = readAll ? 0 : fileID;
    for (uint32 i = 0; i < numFileInfos; i++) {
        const media::fs::FileInfo &fileInfo = m_fsState.GetFileInfoWithOffset(baseFileID + i);
        m_xferBuffer[i * 6 + 0] = fileInfo.frameAddress >> 16u;
        m_xferBuffer[i * 6 + 1] = fileInfo.frameAddress >> 0u;
        m_xferBuffer[i * 6 + 2] = fileInfo.fileSize >> 16u;
        m_xferBuffer[i * 6 + 3] = fileInfo.fileSize >> 0u;
        m_xferBuffer[i * 6 + 4] = (fileInfo.unitSize << 8u) | fileInfo.interleaveGapSize;
        m_xferBuffer[i * 6 + 5] = (fileInfo.fileNumber << 8u) | fileInfo.attributes;
    }

    return numFileInfos;
}

bool CDBlock::SetupSubcodeTransfer(uint8 type) {
    if (!m_cdif.HasDisc()) {
        return false;
    }

    // TODO: check why this is happening
    if (m_status.track == 0xFF) {
        YMIR_DEV_CHECK();
        return false;
    }

    if (type == 0) {
        devlog::trace<grp::xfer>("Starting subcode Q transfer");

        m_xferType = TransferType::Subcode;
        m_xferPos = 0;
        m_xferBufferPos = 0;
        m_xferLength = 5;
        m_xferCount = 0;
        m_xferExtraCount = 0;

        media::DiscPosition pos{};
        if (!m_cdif.ReadPosition(m_status.frameAddress, pos)) {
            // Disc could've been ejected
            return false;
        }

        m_xferBuffer[0] = pos.controlADR;
        m_xferBuffer[1] = pos.track;
        m_xferBuffer[2] = pos.index;
        m_xferBuffer[3] = pos.min;
        m_xferBuffer[4] = pos.sec;
        m_xferBuffer[5] = pos.frame;
        m_xferBuffer[6] = pos.zero;
        m_xferBuffer[7] = pos.amin;
        m_xferBuffer[8] = pos.asec;
        m_xferBuffer[9] = pos.aframe;

        m_RR[0] = GetStatusCode() << 8u;
        m_RR[1] = 5;
        m_RR[2] = 0x0000;
        m_RR[3] = 0x0000;

        return true;
    } else if (type == 1) {
        devlog::trace<grp::xfer>("Starting subcode R-W transfer");

        m_xferType = TransferType::Subcode;
        m_xferPos = 0;
        m_xferBufferPos = 0;
        m_xferLength = 12;
        m_xferCount = 0;
        m_xferExtraCount = 0;

        if (m_status.frameAddress != m_xferSubcodeFrameAddress) {
            m_xferSubcodeFrameAddress = m_status.frameAddress;
            m_xferSubcodeGroup = 0;
        } else {
            m_xferSubcodeGroup++;
        }

        // TODO: read subcode R-W from current sector (24 bytes starting at 2352 + 24*group), & 0x3F all bytes
        // - only works with discs that have 2448 byte sectors
        devlog::trace<grp::xfer>("Subcode R-W transfer is unimplemented");
        m_xferBuffer.fill(0xFF);

        m_RR[0] = GetStatusCode() << 8u;
        m_RR[1] = 12;
        m_RR[2] = 0x0000;
        m_RR[3] = m_xferSubcodeGroup;

        return true;
    }

    return false;
}

void CDBlock::ReadSector() {
    const Buffer *buffer = m_partitionManager.GetTail(m_xferPartition, m_xferSectorPos);
    if (buffer != nullptr) {
        // Skip to user data when not reading 2352 bytes.
        // Also force get sector length 2048 -> 2324 when executing:
        // - Get Sector Data from Mode 2 Form 2 sectors
        // - Get Then Delete Sector Data from Mode 2 sectors
        const bool mode1 = buffer->data[0xF] == 0x01;
        const bool mode2 = buffer->data[0xF] == 0x02;
        const bool mode2form2 = mode2 && bit::test<5>(buffer->data[0x12]);
        const bool mode2GetThenDelete = mode2 && m_xferType == TransferType::GetThenDeleteSector;
        const bool extendLength = mode2form2 || mode2GetThenDelete;
        const uint32 getLength = extendLength ? std::max(2324u, m_getSectorLength) : m_getSectorLength;
        const uint32 limit = mode1 ? 16u : 24u;
        const uint32 offset = std::min(2352u - getLength, limit);

        for (size_t i = 0; i < getLength; i += sizeof(uint16)) {
            m_xferBuffer[i / sizeof(uint16)] = util::ReadBE<uint16>(&buffer->data[i + offset]);
        }
        m_xferGetLength = getLength;

        // Extend total transfer length if the current sector length was extended
        if (extendLength) {
            m_xferLength += m_xferGetLength - m_getSectorLength;
        }

        devlog::trace<grp::xfer>("Starting transfer: partition {}, buffer {}, frame address {:06X} -> {} bytes",
                                 m_xferPartition, m_xferSectorPos, buffer->frameAddress, getLength);
    } else {
        devlog::warn<grp::xfer>("Out of bounds transfer - sector {}", m_xferSectorPos);
        m_xferGetLength = m_getSectorLength;
    }
}

uint16 CDBlock::DoReadTransfer() {
    if (m_xferPos >= m_xferLength) {
        // TODO: what to return here?
        return 0xFFFF;
    }

    uint16 value;
    if (m_xferBufferPos < m_xferBuffer.size()) {
        // TODO: what happens when games attempt to do out-of-bounds reads from TOC of file info transfers?
        value = m_xferBuffer[m_xferBufferPos++];
    } else {
        // TODO: what to return here?
        value = 0xFFFF;
    }

    switch (m_xferType) {
    case TransferType::GetSector: [[fallthrough]];
    case TransferType::GetThenDeleteSector:
        if (m_xferBufferPos >= m_xferGetLength / sizeof(uint16)) {
            ++m_xferSectorPos;
            devlog::trace<grp::xfer>("Going to sector index {}", m_xferSectorPos);
            m_xferBufferPos = 0;
            if (m_xferPos + 1 < m_xferLength) {
                ReadSector();
            }
        }
        break;

    case TransferType::TOC: break;
    case TransferType::FileInfo: break;
    case TransferType::Subcode: break;

    default: ++m_xferExtraCount; return 0; // no active transfer, write-only transfer or unimplemented read transfer
    }

    AdvanceTransfer();

    return value;
}

void CDBlock::DoWriteTransfer(uint16 value) {
    if (m_xferPos >= m_xferLength) {
        return;
    }

    switch (m_xferType) {
    case TransferType::PutSector:
        if (m_scratchBufferPutIndex < m_scratchBuffers.size()) {
            auto &buffer = m_scratchBuffers[m_scratchBufferPutIndex];
            if (m_xferBufferPos < m_putSectorLength) {
                const uint32 writePos = m_xferBufferPos + m_putOffset;
                util::WriteBE<uint16>(&buffer.data[writePos], value);

                // Mode 2 subheader parameters
                if (buffer.data[0xF] == 0x02) {
                    if (writePos == 0x10) {
                        buffer.subheader.fileNum = buffer.data[0x10];
                        buffer.subheader.chanNum = buffer.data[0x11];
                    } else if (writePos == 0x12) {
                        buffer.subheader.submode = buffer.data[0x12];
                        buffer.subheader.codingInfo = buffer.data[0x13];
                    }
                }
            }
        }
        m_xferBufferPos += sizeof(uint16);
        if (m_xferBufferPos >= m_putSectorLength) {
            ++m_scratchBufferPutIndex;
            m_xferBufferPos = 0;
        }
        break;

    default: ++m_xferExtraCount; break; // no active transfer, read-only transfer or unimplemented write transfer
    }

    AdvanceTransfer();
}

void CDBlock::AdvanceTransfer() {
    m_xferPos++;
    m_xferCount++;
    if (m_xferPos >= m_xferLength) {
        devlog::trace<grp::xfer>("Transfer finished - {} of {} words transferred", m_xferCount, m_xferLength);
    }
}

void CDBlock::EndTransfer() {
    if (m_xferType == TransferType::None) {
        return;
    }

    devlog::trace<grp::xfer>("Ending transfer at {} of {} words", m_xferPos, m_xferLength);
    if (m_xferExtraCount > 0) {
        devlog::debug<grp::xfer>("{} unexpected transfer attempts", m_xferExtraCount);
    }

    // Trigger EHST HIRQ if ending certain sector transfers
    switch (m_xferType) {
    case TransferType::GetSector:
    case TransferType::GetThenDeleteSector:
        if (m_xferType == TransferType::GetThenDeleteSector) {
            // Delete sectors, including current sector if not fully read
            const uint32 numFreedSectors =
                m_partitionManager.DeleteSectors(m_xferPartition, m_xferDelStart, m_xferDelCount);
            devlog::trace<grp::xfer>("{} of {} sectors freed", m_xferDelCount, numFreedSectors);
        }
        SetInterrupt(kHIRQ_EHST);
        break;
    case TransferType::PutSector: //
    {
        const uint32 sectorCount = m_xferLength * sizeof(uint16) / m_putSectorLength;
        if (m_partitionManager.UseReservedBuffers(sectorCount)) {
            for (uint32 i = 0; i < sectorCount; ++i) {
                m_partitionManager.InsertHead(m_xferPartition, m_scratchBuffers[i]);
            }
            devlog::trace<grp::xfer>("Sector sent to partition {}", m_xferPartition);
        } else {
            devlog::trace<grp::xfer>("Not enough room to write sector");
        }
        DisconnectCDDevice(m_xferPartition);
        DisconnectFilterInput(m_xferPartition);
        SetInterrupt(kHIRQ_EHST);
        break;
    }
    default: break;
    }

    m_xferType = TransferType::None;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = 0;
    m_xferCount = 0xFFFFFF;
}

bool CDBlock::ConnectCDDevice(uint8 filterNumber) {
    if (filterNumber < m_filters.size()) {
        // Connect CD to specified filter
        DisconnectFilterInput(filterNumber);
        m_cdDeviceConnection = filterNumber;
    } else if (filterNumber == Filter::kDisconnected) {
        // Disconnect CD
        m_cdDeviceConnection = Filter::kDisconnected;
    } else {
        return false;
    }
    return true;
}

bool CDBlock::DisconnectCDDevice(uint8 filterNumber) {
    // Disconnect CD device if connected to the same filter
    if (m_cdDeviceConnection == filterNumber) {
        m_cdDeviceConnection = Filter::kDisconnected;
        devlog::trace<grp::xfer>("CD device disconnected from filter {}", filterNumber);
        return true;
    }
    return false;
}

void CDBlock::DisconnectFilterInput(uint8 filterNumber) {
    for (auto &filter : m_filters) {
        if (filter.failOutput == filterNumber) {
            filter.failOutput = Filter::kDisconnected;
            break; // there can be only one input connection to a filter
        }
    }
}

void CDBlock::SetupCommand() {
    m_scheduler.ScheduleFromNow(m_commandExecEvent, 50);
}

FORCE_INLINE void CDBlock::ProcessCommand() {
    devlog::trace<grp::cmd>("Processing command {:04X} {:04X} {:04X} {:04X}", m_CR[0], m_CR[1], m_CR[2], m_CR[3]);
    TraceProcessCommand(m_tracer, m_CR[0], m_CR[1], m_CR[2], m_CR[3]);

    const uint8 cmd = m_CR[0] >> 8u;

    switch (cmd) {
    case 0x00: CmdGetStatus(); break;
    case 0x01: CmdGetHardwareInfo(); break;
    case 0x02: CmdGetTOC(); break;
    case 0x03: CmdGetSessionInfo(); break;
    case 0x04: CmdInitializeCDSystem(); break;
    case 0x05: CmdOpenTray(); break;
    case 0x06: CmdEndDataTransfer(); break;
    case 0x10: CmdPlayDisc(); break;
    case 0x11: CmdSeekDisc(); break;
    case 0x12: CmdScanDisc(); break;
    case 0x20: CmdGetSubcodeQ_RW(); break;
    case 0x30: CmdSetCDDeviceConnection(); break;
    case 0x31: CmdGetCDDeviceConnection(); break;
    case 0x32: CmdGetLastBufferDest(); break;
    case 0x40: CmdSetFilterRange(); break;
    case 0x41: CmdGetFilterRange(); break;
    case 0x42: CmdSetFilterSubheaderConditions(); break;
    case 0x43: CmdGetFilterSubheaderConditions(); break;
    case 0x44: CmdSetFilterMode(); break;
    case 0x45: CmdGetFilterMode(); break;
    case 0x46: CmdSetFilterConnection(); break;
    case 0x47: CmdGetFilterConnection(); break;
    case 0x48: CmdResetSelector(); break;
    case 0x50: CmdGetBufferSize(); break;
    case 0x51: CmdGetSectorNumber(); break;
    case 0x52: CmdCalculateActualSize(); break;
    case 0x53: CmdGetActualSize(); break;
    case 0x54: CmdGetSectorInfo(); break;
    // case 0x55: CmdExecuteFADSearch(); break;
    // case 0x56: CmdGetFADSearchResults(); break;
    case 0x60: CmdSetSectorLength(); break;
    case 0x61: CmdGetSectorData(); break;
    case 0x62: CmdDeleteSectorData(); break;
    case 0x63: CmdGetThenDeleteSectorData(); break;
    case 0x64: CmdPutSectorData(); break;
    // case 0x65: CmdCopySectorData(); break;
    // case 0x66: CmdMoveSectorData(); break;
    case 0x67: CmdGetCopyError(); break;
    case 0x70: CmdChangeDirectory(); break;
    case 0x71: CmdReadDirectory(); break;
    case 0x72: CmdGetFileSystemScope(); break;
    case 0x73: CmdGetFileInfo(); break;
    case 0x74: CmdReadFile(); break;
    case 0x75: CmdAbortFile(); break;

    // case 0x90: CmdMpegGetStatus(); break;
    // case 0x91: CmdMpegGetInterrupt(); break;
    // case 0x92: CmdMpegSetInterruptMask(); break;
    case 0x93:
        CmdMpegInit();
        break;
        // case 0x94: CmdMpegSetMode(); break;
        // case 0x95: CmdMpegPlay(); break;
        // case 0x96: CmdMpegSetDecodingMethod(); break;
        // case 0x9A: CmdMpegSetConnection(); break;
        // case 0x9B: CmdMpegGetConnection(); break;
        // case 0x9D: CmdMpegSetStream(); break;
        // case 0x9E: CmdMpegGetStream(); break;
        // case 0xA0: CmdMpegDisplay(); break;
        // case 0xA1: CmdMpegSetWindow(); break;
        // case 0xA2: CmdMpegSetBorderColor(); break;
        // case 0xA3: CmdMpegSetFade(); break;
        // case 0xA4: CmdMpegSetVideoEffects(); break;
        // case 0xAF: CmdMpegSetLSI(); break;

    case 0xE0: CmdAuthenticateDevice(); break;
    case 0xE1:
        CmdIsDeviceAuthenticated();
        break;
        // case 0xE2: CmdGetMpegROM(); break;

    default:
        devlog::warn<grp::cmd>("Unimplemented command {:02X}", cmd);
        YMIR_DEV_CHECK();
        ReportCDStatus();
        SetInterrupt(kHIRQ_CMOK);
        break;
    }

    devlog::trace<grp::cmd>("Command response:  {:04X} {:04X} {:04X} {:04X}", m_RR[0], m_RR[1], m_RR[2], m_RR[3]);
    TraceProcessCommandResponse(m_tracer, m_RR[0], m_RR[1], m_RR[2], m_RR[3]);
}

void CDBlock::CmdGetStatus() {
    devlog::trace<grp::cmd>("-> Get status");

    // Input structure:
    // 0x00     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure: standard CD status data
    ReportCDStatus(GetStatusCode());

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetHardwareInfo() {
    devlog::trace<grp::cmd>("-> Get hardware info");

    // Input structure:
    // 0x01     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code      <blank>
    // hardware flags   hardware version
    // <blank>          MPEG version (0 if unauthenticated)
    // drive version    drive revision
    m_RR[0] = GetStatusCode() << 8u;
    m_RR[1] = 0x0002;
    m_RR[2] = 0x0000;
    m_RR[3] = 0x0600;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetTOC() {
    devlog::trace<grp::cmd>("-> Get TOC");

    // Input structure:
    // 0x02     <blank>
    // <blank>
    // <blank>
    // <blank>

    SetupTOCTransfer();

    // Output structure:
    // status code   <blank>
    // TOC size in words
    // <blank>
    // <blank>
    m_RR[0] = GetStatusCode() << 8u;
    m_RR[1] = m_xferLength;
    m_RR[2] = 0x0000;
    m_RR[3] = 0x0000;

    // TODO: make busy for a brief moment
    // NOTE: should *not* change current playback status! Mass Destruction spams this command right after Play Disc,
    // expecting the disc to still play normally.
    // m_status.statusCode = kStatusCodePause;
    // m_targetDriveCycles = kDriveCyclesNotPlaying;

    SetInterrupt(kHIRQ_CMOK | kHIRQ_DRDY);
}

void CDBlock::CmdGetSessionInfo() {
    devlog::trace<grp::cmd>("-> Get session info");

    // Input structure:
    // 0x03     session data type (00 = all, others = specific session)
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code        <blank>
    // <blank>
    // session num/count  lba bits 23-16
    // lba bits 15-0

    const uint8 sessionNum = bit::extract<0, 7>(m_CR[0]);

    // TODO: support multiple sessions if necessary

    m_RR[0] = GetStatusCode() << 8u;
    m_RR[1] = 0x0000;
    if (sessionNum == 0) {
        // Get information about all sessions
        m_RR[2] = (1u << 8u); // TODO: session count
        m_RR[3] = 0x0000;
    } else if (sessionNum == 1) {
        // Get information about a specific session
        const media::TOC &toc = m_cdif.GetTOC();
        const uint32 startFAD = toc.GetStartFrameAddress();
        const uint32 firstTrackNum = toc.GetFirstTrackNumber();
        m_RR[2] = (firstTrackNum << 8u) | bit::extract<16, 23>(startFAD);
        m_RR[3] = bit::extract<0, 15>(startFAD);
    } else {
        // Return FFFFFFFF for nonexistent sessions
        m_RR[2] = 0xFFFF;
        m_RR[3] = 0xFFFF;
    }

    // TODO: make busy for a brief moment
    m_status.statusCode = kStatusCodePause;
    m_targetDriveCycles = kDriveCyclesNotPlaying;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdInitializeCDSystem() {
    devlog::trace<grp::cmd>("-> Initialize CD system");

    // Input structure:
    // 0x04           initialization flags
    // standby time
    // <blank>
    // ECC            retry count
    const bool softReset = bit::test<0>(m_CR[0]);
    // const bool decodeSubcodeRW = bit::test<1>(m_CR[0]);
    // const bool ignoreMode2Subheader = bit::test<2>(m_CR[0]);
    // const bool retryForm2Read = bit::test<3>(m_CR[0]);
    // const uint8 readSpeed = bit::extract<4, 5>(m_CR[0]); // 0=max (2x), 1=2x, 2=invalid, 3=invalid
    // const bool keepSettings = bit::test<7>(m_CR[0]);
    // const uint16 standbyTime = m_CR[1];
    // const uint8 ecc = bit::extract<8, 15>(m_CR[3]);
    // const uint8 retryCount = bit::extract<0, 7>(m_CR[3]);

    const uint8 statusCode = GetStatusCode();
    if (statusCode != kStatusCodeOpen && statusCode != kStatusCodeNoDisc) {
        // TODO: switch to Busy for a bit before NoDisc/Pause
        if (m_cdif.HasDisc()) {
            m_status.statusCode = kStatusCodePause;
        } else {
            m_status.statusCode = kStatusCodeNoDisc;
        }
        m_status.frameAddress = 150;

        // TODO: should it also reset playback state?
    }

    if (softReset) {
        devlog::debug<grp::cmd>("Soft reset");
        // TODO: use Reset(false)
        // TODO: switch to Busy for a bit before NoDisc/Pause
        m_targetDriveCycles = kDriveCyclesNotPlaying;

        // Reset state and configuration
        m_playStartParam = 0xFFFFFF;
        m_playEndParam = 0xFFFFFF;
        m_playRepeatParam = 0;

        m_playStartPos = 0xFFFFFF;
        m_playEndPos = 0xFFFFFF;
        m_playMaxRepeat = 0;
        m_playFile = false;

        m_discAuthStatus = 0;
        m_mpegAuthStatus = 0;

        m_partitionManager.Reset();
        for (auto &filter : m_filters) {
            filter.Reset();
        }
        m_cdDeviceConnection = Filter::kDisconnected;
        m_lastCDWritePartition = 0xFF;

        m_xferType = TransferType::None;
        m_xferPos = 0;
        m_xferLength = 0;
        m_xferCount = 0xFFFFFF;
        m_xferBuffer.fill(0xFFFF);
        m_xferBufferPos = 0;

        m_xferSubcodeFrameAddress = 0;
        m_xferSubcodeGroup = 0;
    }

    // m_readSpeed = readSpeed == 1 ? 1 : m_readSpeedFactor;
    m_readSpeed = m_readSpeedFactor;
    devlog::info<grp::base>("Read speed set to {}x", m_readSpeed);

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_EFLS | kHIRQ_ECPY | kHIRQ_EHST | kHIRQ_ESEL | kHIRQ_CMOK);
}

void CDBlock::CmdOpenTray() {
    devlog::trace<grp::cmd>("-> Open tray");

    // Input structure:
    // 0x05     <blank>
    // <blank>
    // <blank>
    // <blank>

    OpenTray();

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS | kHIRQ_DCHG);
}

void CDBlock::CmdEndDataTransfer() {
    devlog::trace<grp::cmd>("-> End data transfer");

    // Input structure:
    // 0x06     <blank>
    // <blank>
    // <blank>
    // <blank>

    const uint32 transferCount = m_xferCount;

    EndTransfer();

    // Output structure:
    // status code      transferred word count bits 23-16
    // transferred word count bits 15-0
    // <blank>
    // <blank>
    m_RR[0] = (GetStatusCode() << 8u) | (transferCount >> 16u);
    m_RR[1] = transferCount;
    m_RR[2] = 0x0000;
    m_RR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdPlayDisc() {
    devlog::trace<grp::cmd>("-> Play disc");

    // Input structure:
    // 0x10           start position bits 23-16
    // start position bits 15-0
    // play mode      end position bits 23-16
    // end position bits 15-0
    const uint8 repeatParam = bit::extract<8, 15>(m_CR[2]);
    const uint32 startParam = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
    const uint32 endParam = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    devlog::debug<grp::base>("Play parameters: start={:06X} end={:06X} repeat={:X}", startParam, endParam, repeatParam);

    // Output structure: standard CD status data
    if (SetupGenericPlayback(startParam, endParam, repeatParam)) {
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSeekDisc() {
    devlog::trace<grp::cmd>("-> Seek disc");

    // Input structure:
    // 0x11           start position bits 23-16
    // start position bits 15-0
    // <blank>
    // <blank>
    const uint32 startPos = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
    const bool isStartFAD = bit::test<23>(startPos);

    devlog::trace<grp::base>("Seek position: {:06X}", startPos);
    if (startPos == 0xFFFFFF) {
        devlog::debug<grp::base>("Paused");
        m_status.statusCode = kStatusCodePause;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_bufferFullPause = false;
    } else if (startPos == 0x000000) {
        devlog::debug<grp::base>("Stopped");
        m_status.statusCode = kStatusCodeStandby;
        m_status.frameAddress = 0xFFFFFF;
        m_status.flags = 0xF;
        m_status.repeatCount = 0xF;
        m_status.controlADR = 0xFF;
        m_status.track = 0xFF;
        m_status.index = 0xFF;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_cdif.HintStop();
    } else if (isStartFAD) {
        uint32 frameAddress = startPos & 0x7FFFFF;
        devlog::debug<grp::base>("Seeking to frame address {:06X}", frameAddress);
        if (m_cdif.HasDisc()) {
            const media::TOC &toc = m_cdif.GetTOC();

            // Handle frame address exceptions:
            //   Before start of disc (150) -> clamp to 150
            //   After end of disc -> clamp to last disc FAD + 1 (leadout area)
            frameAddress = std::max<uint32>(frameAddress, toc.GetStartFrameAddress());
            const media::TrackInfo *track = toc.GetTrackInfoForFAD(frameAddress);
            if (track != nullptr) {
                m_status.statusCode = kStatusCodePause;
                m_status.frameAddress = frameAddress;
                m_status.flags = track->controlADR == 0x41 ? 0x8 : 0x0;
                m_status.controlADR = track->controlADR;
                m_status.track = track->number;
                m_status.index = 1;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
            } else { // frameAddress > session.endFrameAddress
                devlog::debug<grp::base>("Seeking to leadout area");
                m_status.statusCode = kStatusCodePause;
                m_status.frameAddress = toc.GetLeadOutFrameAddress();
                m_status.flags = 0x0;
                m_status.controlADR = 0x01;
                m_status.track = 0xAA;
                m_status.index = 1;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
            }
        } else {
            devlog::debug<grp::base>("No disc in drive - stopped");
            m_status.statusCode = kStatusCodeNoDisc;
            m_status.frameAddress = 0xFFFFFF;
            m_status.flags = 0xF;
            m_status.repeatCount = 0xF;
            m_status.controlADR = 0xFF;
            m_status.track = 0xFF;
            m_status.index = 0xFF;
            m_targetDriveCycles = kDriveCyclesNotPlaying;
            m_cdif.HintStop();
        }
    } else {
        uint32 trackNum = bit::extract<8, 14>(startPos);
        uint32 indexNum = bit::extract<0, 6>(startPos);
        devlog::debug<grp::base>("Seeking to track:index {}:{}", trackNum, indexNum);
        if (m_cdif.HasDisc()) {
            const media::TOC &toc = m_cdif.GetTOC();
            const uint8 firstTrackNum = toc.GetFirstTrackNumber();
            const uint8 lastTrackNum = toc.GetLastTrackNumber();

            // Handle track number exceptions:
            //   0 -> first track
            //   Outside of valid track range -> clamp to range, set index = 1
            if (trackNum == 0) {
                trackNum = firstTrackNum;
            } else if (trackNum < firstTrackNum) {
                trackNum = firstTrackNum;
                indexNum = 1;
            } else if (trackNum > lastTrackNum) {
                trackNum = lastTrackNum;
                indexNum = 1;
            }

            // Handle index number exceptions:
            //   0 -> 1
            //   Nonexistent index -> start of next track, set index = 1
            if (indexNum == 0) {
                indexNum = 1;
            } else if (indexNum > 99) {
                indexNum = 1;
                if (trackNum < lastTrackNum) {
                    ++trackNum;
                }
            }

            const media::TrackInfo *track = toc.GetTrackInfoForNumber(trackNum);
            if (track != nullptr) {
                m_status.statusCode = kStatusCodePause;
                m_status.frameAddress = track->startFrameAddress;
                m_status.flags = track->controlADR == 0x41 ? 0x8 : 0x0;
                m_status.controlADR = track->controlADR;
                m_status.track = trackNum;
                m_status.index = indexNum;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
            } else {
                devlog::debug<grp::base>("Could not find track - stopped");
                m_status.statusCode = kStatusCodeNoDisc;
                m_status.frameAddress = 0xFFFFFF;
                m_status.flags = 0xF;
                m_status.repeatCount = 0xF;
                m_status.controlADR = 0xFF;
                m_status.track = 0xFF;
                m_status.index = 0xFF;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
                m_cdif.HintStop();
            }
        } else {
            devlog::debug<grp::base>("No disc in drive - stopped");
            m_status.statusCode = kStatusCodeNoDisc;
            m_status.frameAddress = 0xFFFFFF;
            m_status.flags = 0xF;
            m_status.repeatCount = 0xF;
            m_status.controlADR = 0xFF;
            m_status.track = 0xFF;
            m_status.index = 0xFF;
            m_targetDriveCycles = kDriveCyclesNotPlaying;
            m_cdif.HintStop();
        }
    }

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdScanDisc() {
    devlog::trace<grp::cmd>("-> Scan disc");

    // Input structure:
    // 0x12     scan direction
    // <blank>
    // <blank>
    // <blank>
    const uint8 direction = bit::extract<0, 7>(m_CR[0]);

    // Output structure: standard CD status data
    if (SetupScan(direction)) {
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetSubcodeQ_RW() {
    devlog::trace<grp::cmd>("-> Get Subcode Q/RW");

    // Input structure:
    // 0x20     type
    // <blank>
    // <blank>
    // <blank>
    const uint8 type = bit::extract<0, 7>(m_CR[0]);

    // TODO: handle types
    //   type 0 = Q subcode
    //   type 1 = R-W subcodes
    if (SetupSubcodeTransfer(type)) {
        // Output structure if valid (handled by SetupSubcodeTransfer):
        // status code     <blank>
        // Q/RW size in words (Q = 5, RW = 12)
        // <blank>
        // subcode flags

        SetInterrupt(kHIRQ_DRDY);
    } else {
        // Output structure if invalid:
        // 0x80   <blank>
        // <blank>
        // <blank>
        // <blank>
        m_RR[0] = 0x8000;
        m_RR[1] = 0x0000;
        m_RR[2] = 0x0000;
        m_RR[3] = 0x0000;
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetCDDeviceConnection() {
    devlog::trace<grp::cmd>("-> Set CD device connection");

    // Input structure:
    // 0x30           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    // Output structure: standard CD status data
    if (ConnectCDDevice(filterNumber)) {
        devlog::debug<grp::base>("CD device connected to filter {}", filterNumber);
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetCDDeviceConnection() {
    devlog::trace<grp::cmd>("-> Get CD device connection");

    // Input structure:
    // 0x31           <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code    <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    m_RR[0] = GetStatusCode() << 8u;
    m_RR[1] = 0x0000;
    m_RR[2] = m_cdDeviceConnection << 8u;
    m_RR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetLastBufferDest() {
    devlog::trace<grp::cmd>("-> Get last buffer destination");

    // Input structure:
    // 0x32     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code        <blank>
    // <blank>
    // partition number   <blank>
    // <blank>
    m_RR[0] = GetStatusCode() << 8u;
    m_RR[1] = 0x0000;
    m_RR[2] = m_lastCDWritePartition << 8u;
    m_RR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetFilterRange() {
    devlog::trace<grp::cmd>("-> Set filter range");

    // Input structure:
    // 0x40           start frame address bits 23-16
    // start frame address bits 15-0
    // filter number  frame address count bits 23-16
    // frame address count bits 15-0
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        const uint32 start = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
        const uint32 count = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];
        auto &filter = m_filters[filterNumber];
        filter.startFrameAddress = start;
        filter.frameAddressCount = count;

        devlog::debug<grp::base>("Filter {} FAD range: start={:06X} count={:06X}", filterNumber, start, count);

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterRange() {
    devlog::trace<grp::cmd>("-> Get filter range");

    // Input structure:
    // 0x41           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    start frame address bits 23-16
        // start frame address bits 15-0
        // filter number  frame address count bits 23-16
        // frame address count bits 15-0
        const auto &filter = m_filters[filterNumber];
        m_RR[0] = (GetStatusCode() << 8u) | (filter.startFrameAddress >> 16u);
        m_RR[1] = filter.startFrameAddress;
        m_RR[2] = (filterNumber << 8u) | (filter.frameAddressCount >> 16u);
        m_RR[3] = filter.frameAddressCount;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetFilterSubheaderConditions() {
    devlog::trace<grp::cmd>("-> Set filter subheader conditions");

    // Input structure:
    // 0x42           channel
    // submode mask   coding info mask
    // filter number  file ID
    // submode value  coding info value
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        const uint8 chanNum = bit::extract<0, 7>(m_CR[0]);
        const uint8 submodeMask = bit::extract<8, 15>(m_CR[1]);
        const uint8 codingInfoMask = bit::extract<0, 7>(m_CR[1]);
        const uint8 fileID = bit::extract<0, 7>(m_CR[2]);
        const uint8 submodeValue = bit::extract<8, 15>(m_CR[3]);
        const uint8 codingInfoValue = bit::extract<0, 7>(m_CR[3]);

        auto &filter = m_filters[filterNumber];
        filter.chanNum = chanNum;
        filter.fileNum = fileID;
        filter.submodeMask = submodeMask;
        filter.submodeValue = submodeValue;
        filter.codingInfoMask = codingInfoMask;
        filter.codingInfoValue = codingInfoValue;

        devlog::debug<grp::base>(
            "Filter {} subheader conditions: chanNum={:02X} fileNum={:02X} smmask={:02X} smval={:02X} "
            "cimask={:02X} cival={:02X}",
            filterNumber, chanNum, fileID, submodeMask, submodeValue, codingInfoMask, codingInfoValue);

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterSubheaderConditions() {
    devlog::trace<grp::cmd>("-> Get filter subheader conditions");

    // Input structure:
    // 0x43           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    channel
        // submode mask   coding info mask
        // filter number  file ID
        // submode value  coding info value
        const auto &filter = m_filters[filterNumber];
        m_RR[0] = (GetStatusCode() << 8u) | filter.chanNum;
        m_RR[1] = (filter.submodeMask << 8u) | filter.codingInfoMask;
        m_RR[2] = (filterNumber << 8u) | filter.fileNum;
        m_RR[3] = (filter.submodeValue << 8u) | filter.codingInfoValue;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdSetFilterMode() {
    devlog::trace<grp::cmd>("-> Set filter mode");

    // Input structure:
    // 0x44           mode
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        const uint8 mode = bit::extract<0, 7>(m_CR[0]);

        auto &filter = m_filters[filterNumber];
        filter.mode = mode & 0x5F; // TODO: should it be masked?

        devlog::debug<grp::base>(
            "Filter {} mode={:02X}{}{}{}{}{}{}", filterNumber, filter.mode,
            (bit::test<0>(filter.mode) ? " filenum" : ""), (bit::test<1>(filter.mode) ? " channum" : ""),
            (bit::test<2>(filter.mode) ? " submode" : ""), (bit::test<3>(filter.mode) ? " codinginfo" : ""),
            (bit::test<4>(filter.mode) ? " <- inverted" : ""), (bit::test<6>(filter.mode) ? " fad" : ""));

        if (mode & 0x80) {
            devlog::debug<grp::base>("Filter {} conditions reset", filterNumber);
            filter.ResetConditions();
        }

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterMode() {
    devlog::trace<grp::cmd>("-> Get filter mode");

    // Input structure:
    // 0x45           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    mode
        // <blank>
        // filter number  <blank>
        // <blank>
        const auto &filter = m_filters[filterNumber];
        m_RR[0] = (GetStatusCode() << 8u) | filter.mode;
        m_RR[1] = 0x0000;
        m_RR[2] = (filterNumber << 8u);
        m_RR[3] = 0x0000;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdSetFilterConnection() {
    devlog::trace<grp::cmd>("-> Set filter connection");

    // Input structure:
    // 0x46           connection flags
    // pass conn      fail conn
    // filter number  <blank>
    // <blank>
    const bool setPassConn = bit::test<0>(m_CR[0]);
    const bool setFailConn = bit::test<1>(m_CR[0]);
    const uint8 passConn = bit::extract<8, 15>(m_CR[1]);
    const uint8 failConn = bit::extract<0, 7>(m_CR[1]);
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        auto &filter = m_filters[filterNumber];
        if (setPassConn) {
            devlog::debug<grp::base>("Filter {} pass output={:02X}", filterNumber, passConn);
            filter.passOutput = passConn;
        }
        if (setFailConn) {
            devlog::debug<grp::base>("Filter {} fail output={:02X}", filterNumber, failConn);
            DisconnectFilterInput(failConn);
            filter.failOutput = failConn;
        }

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterConnection() {
    devlog::trace<grp::cmd>("-> Get filter connection");

    // Input structure:
    // 0x47           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    <blank>
        // pass conn      fail conn
        // filter number  <blank>
        // <blank>
        const auto &filter = m_filters[filterNumber];
        m_RR[0] = (GetStatusCode() << 8u);
        m_RR[1] = (filter.passOutput << 8u) | filter.failOutput;
        m_RR[2] = (filterNumber << 8u);
        m_RR[3] = 0x0000;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdResetSelector() {
    devlog::trace<grp::cmd>("-> Reset selector");

    // Input structure:
    // 0x48              reset flags
    // <blank>
    // partition number  <blank>
    // <blank>
    const uint8 resetFlags = bit::extract<0, 7>(m_CR[0]);

    bool reject = false;
    if (resetFlags == 0) {
        const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
        devlog::trace<grp::base>("Clearing buffer partition {}", partitionNumber);
        if (partitionNumber < kNumFilters) {
            m_partitionManager.Clear(partitionNumber);
        } else {
            reject = true;
        }
    } else {
        const bool clearBufferData = bit::test<2>(resetFlags);
        const bool clearPartitionOutputs = bit::test<3>(resetFlags);
        const bool clearFilterConditions = bit::test<4>(resetFlags);
        const bool clearFilterInputs = bit::test<5>(resetFlags);
        const bool clearFilterPassOutputs = bit::test<6>(resetFlags);
        const bool clearFilterFailOutputs = bit::test<7>(resetFlags);

        if (clearBufferData) {
            devlog::debug<grp::base>("Clearing all buffer partitions");
            m_partitionManager.Reset();
        }
        if (clearPartitionOutputs) {
            devlog::debug<grp::base>("Clearing all partition output connectors");
            // TODO: clear device inputs and filter inputs connected to partition outputs
        }
        if (clearFilterConditions) {
            devlog::debug<grp::base>("Clearing all filter conditions");
            for (auto &filter : m_filters) {
                filter.ResetConditions();
            }
        }
        if (clearFilterInputs) {
            devlog::debug<grp::base>("Clearing all filter input connectors");
            for (auto &filter : m_filters) {
                filter.failOutput = Filter::kDisconnected;
            }
            m_cdDeviceConnection = Filter::kDisconnected;
        }
        if (clearFilterPassOutputs) {
            devlog::debug<grp::base>("Clearing all filter pass output connectors");
            for (int i = 0; auto &filter : m_filters) {
                filter.passOutput = i;
                i++;
            }
        }
        if (clearFilterFailOutputs) {
            devlog::debug<grp::base>("Clearing all filter fail output connectors");
            for (auto &filter : m_filters) {
                filter.failOutput = Filter::kDisconnected;
            }
        }
    }

    // Output structure: standard CD status data
    if (reject) {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetBufferSize() {
    devlog::trace<grp::cmd>("-> Get buffer size");

    // Input structure:
    // 0x50     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code          <blank>
    // free buffer count
    // total filter count   <blank>
    // total buffer count
    const uint32 freeBuffers = m_partitionManager.GetFreeBufferCount();
    m_RR[0] = GetStatusCode() << 8u;
    m_RR[1] = freeBuffers;
    m_RR[2] = kNumFilters << 8u;
    m_RR[3] = kNumBuffers;

    devlog::trace<grp::base>("Get buffer size: free buffers = {}", freeBuffers);

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetSectorNumber() {
    devlog::trace<grp::cmd>("-> Get sector number");

    // Input structure:
    // 0x51              <blank>
    // <blank>
    // partition number  <blank>
    // <blank>
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    uint8 sectorCount = m_partitionManager.GetBufferCount(partitionNumber);
    if (m_xferType == TransferType::GetThenDeleteSector) {
        sectorCount -= m_xferDelCount;
    }

    // Output structure:
    // status code      <blank>
    // <blank>
    // <blank>
    // number of blocks
    m_RR[0] = (GetStatusCode() << 8u);
    m_RR[1] = 0x0000;
    m_RR[2] = 0x0000;
    m_RR[3] = sectorCount;

    devlog::trace<grp::base>("Partition {} has {} sectors", partitionNumber, sectorCount);

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdCalculateActualSize() {
    devlog::trace<grp::cmd>("-> Calculate actual size");

    // Input structure:
    // 0x52               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    uint16 sectorNumber = m_CR[3];

    bool reject = false;
    bool wait = false;
    if (partitionNumber > kNumPartitions) [[unlikely]] {
        reject = true;
    } else {
        const uint8 bufferCount = m_partitionManager.GetBufferCount(partitionNumber);
        if (sectorOffset == 0xFFFF) {
            sectorOffset = bufferCount - 1u;
        }
        if (sectorNumber == 0xFFFF) {
            sectorNumber = bufferCount - sectorOffset;
        }
        if (sectorOffset >= bufferCount || sectorNumber == 0 || sectorOffset + sectorNumber > bufferCount) {
            wait = true;
        } else {
            const uint16 startSector = sectorOffset;
            const uint16 endSector = sectorOffset + sectorNumber - 1u;
            m_calculatedPartitionSize =
                m_partitionManager.CalculateSize(partitionNumber, startSector, endSector, m_getSectorLength) /
                sizeof(uint16);
            devlog::trace<grp::base>("Actual size of partition {} from sector {} to {} = {} words", partitionNumber,
                                     startSector, endSector, m_calculatedPartitionSize);
        }
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else if (wait) [[unlikely]] {
        ReportCDStatus(GetStatusCode() | kStatusFlagWait);
    } else {
        ReportCDStatus();
    }

    uint16 hirq = kHIRQ_CMOK;
    if (!reject && !wait) {
        hirq |= kHIRQ_ESEL;
    }
    SetInterrupt(hirq);
}

void CDBlock::CmdGetActualSize() {
    devlog::trace<grp::cmd>("-> Get actual size");

    // Input structure:
    // 0x53     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code   calculated size bits 23-16 (in words)
    // calculated size bits 15-0 (in words)
    // <blank>
    // <blank>
    m_RR[0] = (GetStatusCode() << 8u) | bit::extract<16, 23>(m_calculatedPartitionSize);
    m_RR[1] = bit::extract<0, 15>(m_calculatedPartitionSize);
    m_RR[2] = 0x0000;
    m_RR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetSectorInfo() {
    devlog::trace<grp::cmd>("-> Get sector info");

    // Input structure:
    // 0x54               <blank>
    // <blank>            sector number
    // partition number   <blank>
    // <blank>
    const uint8 sectorNumber = bit::extract<0, 7>(m_CR[1]);
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);

    bool reject = false;
    if (partitionNumber > kNumPartitions) [[unlikely]] {
        reject = true;
    } else {
        const Buffer *buffer = m_partitionManager.GetTail(partitionNumber, sectorNumber);
        if (buffer == nullptr) {
            reject = true;
        } else {
            // Output structure:
            // status code          sector frame address bits 23-16
            // sector frame address bits 15-0
            // sector file number   sector coding number
            // sector submode       sector coding info
            m_RR[0] = (GetStatusCode() << 8u) | (buffer->frameAddress >> 16u);
            m_RR[1] = buffer->frameAddress;
            m_RR[2] = (buffer->subheader.fileNum << 8u) | buffer->subheader.chanNum;
            m_RR[3] = (buffer->subheader.submode << 8u) | buffer->subheader.codingInfo;
        }
    }

    if (reject) {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdExecuteFADSearch() {
    devlog::trace<grp::cmd>("-> Execute frame address search");

    // Input structure:
    // 0x55     <blank>
    // sector position
    // partition number   frame address bits 23-16
    // frame address bits 15-0
    // const uint16 sectorPos = m_CR[1];
    // const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    // const uint32 frameAddress = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    // TODO: search for a sector with the largest FAD <= searched FAD within specified partition
    // - how does sectorPos factor in here?
    devlog::info<grp::base>("Execute frame address search command is unimplemented");
    YMIR_DEV_CHECK();

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFADSearchResults() {
    devlog::trace<grp::cmd>("-> Get frame address search results");

    // Input structure:
    // 0x56     <blank>
    // <blank>
    // <blank>
    // <blank>

    // TODO: return search FAD results
    devlog::info<grp::base>("Get frame address search results command is unimplemented");
    YMIR_DEV_CHECK();

    // Output structure:
    // status code        <blank>
    // sector position
    // partition number   frame address bits 23-16
    // frame address bits 15-0
    m_RR[0] = (GetStatusCode() << 8u);
    m_RR[1] = 0; // TODO: sector position
    m_RR[2] = 0; // TODO: partition number, FAD high
    m_RR[3] = 0; // TODO: FAD low

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetSectorLength() {
    devlog::trace<grp::cmd>("-> Set sector length");

    // Input structure:
    // 0x60               get sector length
    // put sector length  <blank>
    // <blank>
    // <blank>
    const uint8 getSectorLength = bit::extract<0, 7>(m_CR[0]);
    const uint8 putSectorLength = bit::extract<8, 15>(m_CR[1]);

    static constexpr uint32 kSectorLengths[] = {
        2048, // user data
        2336, // + subheader (checksum, ECC)
        2340, // + header (sector offset and mode)
        2352, // + sync bytes
    };

    if (getSectorLength < 4) {
        m_getSectorLength = kSectorLengths[getSectorLength];
    }
    if (putSectorLength < 4) {
        m_putSectorLength = kSectorLengths[putSectorLength];
    }
    m_putOffset = CalcPutOffset(m_putSectorLength);
    devlog::debug<grp::base>("Sector lengths: get={} put={}", m_getSectorLength, m_putSectorLength);

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetSectorData() {
    devlog::trace<grp::cmd>("-> Get sector data");

    // Input structure:
    // 0x61               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    const uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    const SectorTransferResult result = SetupGetSectorTransfer(sectorOffset, sectorNumber, partitionNumber, false);

    uint16 hirq = kHIRQ_CMOK;

    // Output structure: standard CD status data
    switch (result) {
    case SectorTransferResult::OK:
        // TODO: should hold status flag kStatusFlagXferRequest until ready
        ReportCDStatus(GetStatusCode() | kStatusFlagXferRequest);
        hirq |= kHIRQ_DRDY;
        break;
    case SectorTransferResult::Wait: ReportCDStatus(GetStatusCode() | kStatusFlagWait); break;
    case SectorTransferResult::Reject: [[fallthrough]];
    default: ReportCDStatus(kStatusReject); break;
    }

    SetInterrupt(hirq);
}

void CDBlock::CmdDeleteSectorData() {
    devlog::trace<grp::cmd>("-> Delete sector data");

    // Input structure:
    // 0x62               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    const uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    bool reject = false;
    bool wait = false;
    if (partitionNumber >= kNumPartitions) [[unlikely]] {
        devlog::trace<grp::base>("Delete sector rejected: invalid partition {}", partitionNumber);
        reject = true;
    } else {
        const uint32 partSecCount = m_partitionManager.GetBufferCount(partitionNumber);

        const uint32 startSector = sectorOffset == 0xFFFF ? partSecCount - 1 : sectorOffset;
        const uint32 endSector = sectorNumber == 0xFFFF ? partSecCount - 1 : startSector + sectorNumber - 1;

        if (sectorNumber == 0) {
            devlog::trace<grp::base>("Delete sector rejected: requested zero sectors");
            wait = true;
        } else if (partSecCount == 0) [[unlikely]] {
            devlog::trace<grp::base>("Delete sector rejected: no data in partition {}", partitionNumber);
            reject = true;
        } else if (startSector >= partSecCount || endSector >= partSecCount) {
            devlog::trace<grp::base>("Delete sector rejected: sectors out of range ({}..{} > {})", startSector,
                                     endSector, partSecCount);
            wait = true;
        } else if (startSector > endSector) {
            devlog::trace<grp::base>("Delete sector rejected: sectors range reversed: {}..{}", startSector, endSector);
            reject = true;
        } else {
            const uint32 numFreedSectors =
                m_partitionManager.DeleteSectors(partitionNumber, sectorOffset, sectorNumber);
            devlog::trace<grp::base>("Freed {} sectors from partition {} at offset {}", numFreedSectors,
                                     partitionNumber, sectorOffset);
        }
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else if (wait) [[unlikely]] {
        ReportCDStatus(GetStatusCode() | kStatusFlagWait);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EHST);
}

void CDBlock::CmdGetThenDeleteSectorData() {
    devlog::trace<grp::cmd>("-> Get then delete sector data");

    // Input structure:
    // 0x63               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    const uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    const SectorTransferResult result = SetupGetSectorTransfer(sectorOffset, sectorNumber, partitionNumber, true);

    uint16 hirq = kHIRQ_CMOK;

    // Output structure: standard CD status data
    switch (result) {
    case SectorTransferResult::OK:
        // TODO: should hold status flag kStatusFlagXferRequest until ready
        ReportCDStatus(GetStatusCode() | kStatusFlagXferRequest);
        hirq |= kHIRQ_DRDY;
        break;
    case SectorTransferResult::Wait: ReportCDStatus(GetStatusCode() | kStatusFlagWait); break;
    case SectorTransferResult::Reject: [[fallthrough]];
    default: ReportCDStatus(kStatusReject); break;
    }

    SetInterrupt(hirq);
}

void CDBlock::CmdPutSectorData() {
    devlog::trace<grp::cmd>("-> Put sector data");

    // Input structure:
    // 0x64               <blank>
    // <blank>
    // partition number   <blank>
    // sector number
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    bool reject = false;
    bool wait = false;
    if (partitionNumber >= kNumPartitions) [[unlikely]] {
        devlog::trace<grp::base>("Put sector transfer rejected: invalid partition {}", partitionNumber);
        reject = true;
    } else if (!m_partitionManager.ReserveBuffers(sectorNumber)) [[unlikely]] {
        devlog::trace<grp::base>("Put sector transfer rejected: not enough free buffers available");
        wait = true;
    } else {
        SetupPutSectorTransfer(sectorNumber, partitionNumber);
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else if (wait) [[unlikely]] {
        ReportCDStatus(GetStatusCode() | kStatusFlagWait);
    } else {
        ReportCDStatus(GetStatusCode() | kStatusFlagXferRequest);
        // TODO: should hold status flag kStatusFlagXferRequest until ready
    }

    uint16 hirq = kHIRQ_CMOK;
    if (!reject && !wait) {
        hirq |= kHIRQ_DRDY;
    }
    SetInterrupt(hirq);
}

void CDBlock::CmdCopySectorData() {
    devlog::trace<grp::cmd>("-> Copy sector data");

    // Input structure:
    // 0x65                      destination filter number
    // sector offset
    // source partition number   <blank>
    // sector number
    // const uint8 dstPartitionNumber = bit::extract<0, 7>(m_CR[0]);
    // const uint16 sectorOffset = m_CR[1];
    // const uint8 srcPartitionNumber = bit::extract<8, 15>(m_CR[2]);
    // const uint16 sectorNumber = m_CR[3];

    // TODO: setup async sector copy transfer
    // TODO: report Reject status if not enough buffer space available
    devlog::info<grp::base>("Copy sector data command is unimplemented");
    YMIR_DEV_CHECK();

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ECPY);
}

void CDBlock::CmdMoveSectorData() {
    devlog::trace<grp::cmd>("-> Move sector data");

    // Input structure:
    // 0x66                      destination filter number
    // sector offset
    // source partition number   <blank>
    // sector number
    // const uint8 dstPartitionNumber = bit::extract<0, 7>(m_CR[0]);
    // const uint16 sectorOffset = m_CR[1];
    // const uint8 srcPartitionNumber = bit::extract<8, 15>(m_CR[2]);
    // const uint16 sectorNumber = m_CR[3];

    // TODO: setup async sector move transfer
    devlog::info<grp::base>("Move sector data command is unimplemented");
    YMIR_DEV_CHECK();

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ECPY);
}

void CDBlock::CmdGetCopyError() {
    devlog::trace<grp::cmd>("-> Get copy error");

    // Input structure:
    // 0x67     <blank>
    // <blank>
    // <blank>
    // <blank>

    devlog::info<grp::base>("Get copy error command is unimplemented");

    // Output structure:
    // status code   error code
    // <blank>
    // <blank>
    // <blank>
    m_RR[0] = (GetStatusCode() << 8u) | 0x00; // TODO: async copy/move error code
    m_RR[1] = 0x0000;
    m_RR[2] = 0x0000;
    m_RR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdChangeDirectory() {
    devlog::trace<grp::cmd>("-> Change directory");

    // Input structure:
    // 0x70            <blank>
    // <blank>
    // filter number   file ID bits 23-16
    // file ID bits 15-0
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);
    const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    bool reject = false;
    if (filterNumber < m_filters.size()) {
        // TODO: use filter to read the sector(s) containing the directory record
        reject = !m_fsState.ChangeDirectory(fileID);
        if (!reject) {
            devlog::debug<grp::base>("Changed directory to {} (file ID {:X}) using filter {}",
                                     m_fsState.GetCurrentPath(), fileID, filterNumber);
        }
    } else if (filterNumber == 0xFF) {
        reject = true;
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdReadDirectory() {
    devlog::trace<grp::cmd>("-> Read directory");

    // Input structure:
    // 0x71            <blank>
    // <blank>
    // filter number   file ID bits 23-16
    // file ID bits 15-0
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);
    const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    bool reject = false;
    if (filterNumber < m_filters.size()) {
        // TODO: use filter to read the sector(s) containing the directory record
        reject = !m_fsState.ReadDirectory(fileID);
        if (!reject) {
            devlog::debug<grp::base>("Reading directory {} (file ID {:X}) using filter {}",
                                     m_fsState.GetFileInfo(fileID).name, fileID, filterNumber);
        }
    } else if (filterNumber == 0xFF) {
        reject = true;
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdGetFileSystemScope() {
    devlog::trace<grp::cmd>("-> Get file system scope");

    // Input structure:
    // 0x72     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code            <blank>
    // index number
    // directory end offset   first file ID bits 23-16
    // first file ID bits 15-0

    const uint32 fileOffset = m_fsState.GetFileOffset() + 2;
    const uint32 fileCount = m_fsState.GetFileCount();
    const bool endOfDirectory = fileOffset + 254 >= fileCount;
    m_RR[0] = GetStatusCode() << 8u;
    m_RR[1] = fileCount;
    m_RR[2] = (endOfDirectory << 8u) | bit::extract<16, 23>(fileOffset);
    m_RR[3] = bit::extract<0, 15>(fileOffset);

    devlog::trace<grp::base>("Get file system scope: {} files from offset {}, {}", fileCount, fileOffset,
                             (endOfDirectory ? "end of list" : "more files available"));

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdGetFileInfo() {
    devlog::trace<grp::cmd>("-> Get file info");

    // Input structure:
    // 0x73     <blank>
    // <blank>
    // <blank>  file ID bits 23-16
    // file ID bits 15-0
    const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    bool reject = false;
    if (!m_fs.IsValid() || !m_fsState.HasCurrentDirectory()) {
        reject = true;
    }
    if (fileID != 0xFFFFFF && fileID > 2 + m_fsState.GetFileCount() - m_fsState.GetFileOffset()) {
        reject = true;
    }

    if (!reject) [[likely]] {
        const uint32 numFileInfos = SetupFileInfoTransfer(fileID);

        // Output structure:
        // status code            <blank>
        // file info size in words
        // <blank>
        // <blank>
        m_RR[0] = GetStatusCode() << 8u;
        m_RR[1] = numFileInfos * 12 / sizeof(uint16);
        m_RR[2] = 0x0000;
        m_RR[3] = 0x0000;
        SetInterrupt(kHIRQ_DRDY);
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdReadFile() {
    devlog::trace<grp::cmd>("-> Read file");

    // Input structure:
    // 0x74            offset bits 23-16
    // offset bits 15-0
    // filter number   file ID bits 23-16
    // file ID bits 15-0
    const uint32 offset = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);
    const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    if (SetupFilePlayback(fileID, offset, filterNumber)) {
        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_DRDY);
}

void CDBlock::CmdAbortFile() {
    devlog::trace<grp::cmd>("-> Abort file");

    // Input structure:
    // 0x75     <blank>
    // <blank>
    // <blank>
    // <blank>

    devlog::debug<grp::base>("Aborting transfer");

    EndTransfer();

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdMpegGetStatus() {}

void CDBlock::CmdMpegGetInterrupt() {}

void CDBlock::CmdMpegSetInterruptMask() {}

void CDBlock::CmdMpegInit() {
    devlog::trace<grp::cmd>("-> MPEG init");

    // Input structure:
    // 0x93     <blank>
    // <blank>
    // <blank>
    // <blank>

    // TODO: initialize MPEG stuff
    devlog::info<grp::base>("MPEG init command is unimplemented");
    YMIR_DEV_CHECK();

    // Output structure:
    // status code (FF=unauthenticated)  <blank>
    // <blank>
    // <blank>
    // <blank>
    m_RR[0] = 0xFF00;
    m_RR[1] = 0;
    m_RR[2] = 0;
    m_RR[3] = 0;

    SetInterrupt(kHIRQ_CMOK | kHIRQ_MPED | kHIRQ_MPST);
}

void CDBlock::CmdMpegSetMode() {}

void CDBlock::CmdMpegPlay() {}

void CDBlock::CmdMpegSetDecodingMethod() {}

void CDBlock::CmdMpegSetConnection() {}

void CDBlock::CmdMpegGetConnection() {}

void CDBlock::CmdMpegSetStream() {}

void CDBlock::CmdMpegGetStream() {}

void CDBlock::CmdMpegDisplay() {}

void CDBlock::CmdMpegSetWindow() {}

void CDBlock::CmdMpegSetBorderColor() {}

void CDBlock::CmdMpegSetFade() {}

void CDBlock::CmdMpegSetVideoEffects() {}

void CDBlock::CmdMpegSetLSI() {}

void CDBlock::CmdAuthenticateDevice() {
    devlog::trace<grp::cmd>("-> Authenticate device");

    // Input structure:
    // 0xE0    <blank>
    // authentication type (0x0000=CD, 0x0001=MPEG)
    // <blank>
    // <blank>

    const uint16 authType = m_CR[1];

    switch (authType) {
    case 0x0000:
        devlog::debug<grp::base>("Performing CD authentication");
        m_discAuthStatus = 4; // always authenticated ;)
        SetInterrupt(kHIRQ_EFLS | kHIRQ_CSCT);
        break;
    case 0x0001:
        devlog::debug<grp::base>("Performing MPEG authentication");
        m_mpegAuthStatus = 2;
        SetInterrupt(kHIRQ_MPED);
        break;
    default:
        devlog::debug<grp::base>("Authenticate device parameter invalid: unexpected authentication type {}", authType);
        break;
    }

    // TODO: make busy for a brief moment
    m_status.statusCode = kStatusCodePause;
    m_targetDriveCycles = kDriveCyclesNotPlaying;

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdIsDeviceAuthenticated() {
    devlog::trace<grp::cmd>("-> Is device authenticated");

    // Input structure:
    // 0xE1    <blank>
    // authentication type (0x0000=CD, 0x0001=MPEG)
    // <blank>
    // <blank>

    const uint16 authType = m_CR[1];

    // Output structure:
    // status code  <blank>
    // authentication status
    // <blank>
    // <unknown>
    m_RR[0] = (GetStatusCode() << 8u);
    m_RR[1] = authType == 0x0000 ? m_discAuthStatus : m_mpegAuthStatus;
    m_RR[2] = 0;
    m_RR[3] = 0;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetMpegROM() {}

// -----------------------------------------------------------------------------
// Probe implementation

CDBlock::Probe::Probe(CDBlock &cdblock)
    : m_cdblock(cdblock) {}

uint8 CDBlock::Probe::GetCurrentStatusCode() const {
    return m_cdblock.m_status.statusCode & 0xF;
}

uint32 CDBlock::Probe::GetCurrentFrameAddress() const {
    return m_cdblock.m_status.frameAddress;
}

uint8 CDBlock::Probe::GetCurrentRepeatCount() const {
    return m_cdblock.m_status.repeatCount & 0xF;
}

uint8 CDBlock::Probe::GetMaxRepeatCount() const {
    return m_cdblock.m_playMaxRepeat;
}

uint8 CDBlock::Probe::GetCurrentControlADRBits() const {
    return m_cdblock.m_status.controlADR;
}

uint8 CDBlock::Probe::GetCurrentTrack() const {
    return m_cdblock.m_status.track;
}

uint8 CDBlock::Probe::GetCurrentIndex() const {
    return m_cdblock.m_status.index;
}

uint8 CDBlock::Probe::GetReadSpeed() const {
    return m_cdblock.m_readSpeed;
}

uint8 CDBlock::Probe::GetCDDeviceConnection() const {
    return m_cdblock.m_cdDeviceConnection;
}

const media::fs::FilesystemEntry *CDBlock::Probe::GetFileAtFrameAddress(uint32 fad) const {
    return m_cdblock.m_fs.GetFileAtFrameAddress(fad);
}

std::string CDBlock::Probe::GetPathAtFrameAddress(uint32 fad) const {
    return m_cdblock.m_fs.GetPathAtFrameAddress(fad);
}

std::span<const Filter, kNumFilters> CDBlock::Probe::GetFilters() const {
    return m_cdblock.m_filters;
}

} // namespace ymir::cdblock
