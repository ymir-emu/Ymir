#pragma once

#include "cdblock_defs.hpp"

#include "cdblock_buffer.hpp"
#include "cdblock_filter.hpp"

#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>

#include <ymir/sys/bus.hpp>
#include <ymir/sys/clocks.hpp>

#include <ymir/debug/cdblock_tracer_base.hpp>

#include <ymir/hw/cdblock/cdblock_internal_callbacks.hpp>
#include <ymir/sys/system_internal_callbacks.hpp>

#include <ymir/savestate/savestate_cdblock.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/media/cd_interface.hpp>
#include <ymir/media/filesystem.hpp>

#include <ymir/core/hash.hpp>

#include <array>
#include <deque>

namespace ymir::cdblock {

class CDBlock {
public:
    CDBlock(core::Scheduler &scheduler, media::CDInterface &cdif, const media::fs::Filesystem &fs,
            core::Configuration::CDBlock &config);

    void Reset(bool hard);

    void MapCallbacks(CBTriggerExternalInterrupt0 cbTriggerExtIntr0, CBCDDASector cbCDDASector) {
        m_cbTriggerExternalInterrupt0 = cbTriggerExtIntr0;
        m_cbCDDASector = cbCDDASector;
    }

    void MapMemory(sys::SH2Bus &bus);

    void UpdateClockRatios(const sys::ClockRatios &clockRatios);

    void OnDiscLoaded();
    void OnDiscEjected();
    void OpenTray();
    void CloseTray();
    [[nodiscard]] bool IsTrayOpen() const;

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::CDBlockSaveState &state) const;
    [[nodiscard]] bool ValidateState(const savestate::CDBlockSaveState &state) const;
    void LoadState(const savestate::CDBlockSaveState &state);

private:
    CBTriggerExternalInterrupt0 m_cbTriggerExternalInterrupt0;
    CBCDDASector m_cbCDDASector;

    core::Scheduler &m_scheduler;
    core::EventID m_driveStateUpdateEvent;
    core::EventID m_commandExecEvent;
    bool m_eventsEnabled;

    static void OnDriveStateUpdateEvent(core::EventContext &eventContext, void *userContext);
    static void OnCommandExecEvent(core::EventContext &eventContext, void *userContext);

    alignas(uint64) std::array<uint16, 4> m_CR;
    alignas(uint64) std::array<uint16, 4> m_RR;

    media::CDInterface &m_cdif;
    const media::fs::Filesystem &m_fs;
    media::fs::FilesystemState m_fsState{m_fs};

    // -------------------------------------------------------------------------
    // Memory accessors (SCU-facing bus)
    // 16-bit reads, 8- or 16-bit writes

    // TODO: handle 8-bit and 32-bit accesses properly

    template <mem_primitive T>
    T ReadReg(uint32 address);

    template <mem_primitive T>
    void WriteReg(uint32 address, T value);

    template <mem_primitive T>
    T PeekReg(uint32 address);

    template <mem_primitive T>
    void PokeReg(uint32 address, T value);

    // -------------------------------------------------------------------------
    // Disc/drive state

    struct Status {
        // Status code, one of kStatusCode* constants.
        // Never kStatusCodeReject.
        // Does not include kStatusFlag* constants.
        uint8 statusCode;

        uint32 frameAddress; // current frame address
        uint8 flags;         // bit 7: 1=reading CD-ROM data; 0=reading CD-DA, seeking, scanning, etc.
        uint8 repeatCount;   // bits 3-0: repeat count
        uint8 controlADR;    // control/ADR bits of the current track
        uint8 track;         // current track
        uint8 index;         // current index
    } m_status;

    bool m_readyForPeriodicReports; // HACK to avoid overwriting the initial state during the boot sequence

    uint32 m_currDriveCycles;   // current cycle count for drive state processing
    uint32 m_targetDriveCycles; // number of cycles until the next drive state update
    uint32 m_seekTicks;         // number of ticks until Seek transitions to Play

    // PlayDisc/ScanDisc parameters
    uint32 m_playStartParam; // starting frame address or track/index
    uint32 m_playEndParam;   // ending frame address or track/index
    uint8 m_playRepeatParam; // playback repeat count parameter
    bool m_scanDirection;    // scan direction (false=forward, true=backward)
    uint8 m_scanCounter;     // scan frame counter, to determine when to skip sectors

    // Playback status/parameters
    uint32 m_playStartPos;  // starting frame address for playback
    uint32 m_playEndPos;    // ending frame address for playback
    uint8 m_playMaxRepeat;  // max repeat count (0=no repeat, 1..14=N repeats, 15=infinite repeats)
    bool m_playFile;        // is playback reading a file?
    bool m_bufferFullPause; // paused because of running out of buffers?
    bool m_playEndPending;  // is the next Play state update going to end playback?

    uint8 m_readSpeed;
    uint8 m_readSpeedFactor;

    // CD authentication status:
    //   0: no CD/not authenticated
    //   1: audio CD
    //   2: non-Saturn CD
    //   3: non-original Saturn CD
    //   4: original Saturn CD
    uint8 m_discAuthStatus;

    // MPEG authentication status:
    //   0: no MPEG card/not authenticated
    //   2: MPEG card present
    uint8 m_mpegAuthStatus;

    bool SetupGenericPlayback(uint32 startParam, uint32 endParam, uint16 repeatParam);
    bool SetupFilePlayback(uint32 fileID, uint32 offset, uint8 filterNumber);
    bool SetupScan(uint8 direction);

    void ProcessDriveState();
    void ProcessDriveStatePlay();
    void CheckPlayEnd();

    // -------------------------------------------------------------------------
    // Interrupts

    uint16 m_HIRQ;
    uint16 m_HIRQMASK;

    void SetInterrupt(uint16 bits);
    void UpdateInterrupts();

    // -------------------------------------------------------------------------
    // Status reports

    // Updates RR1-4 with the current CD status
    void ReportCDStatus();

    // Updates RR1-4 with the current CD status, overriding the status code
    void ReportCDStatus(uint8 statusCode);

    // Gets the current CD status code without the flags and taking into account the Play->Pause transition period
    uint8 GetStatusCode() const;

    // -------------------------------------------------------------------------
    // Data transfers

    enum class TransferType { None, TOC, GetSector, GetThenDeleteSector, PutSector, FileInfo, Subcode };

    // General transfer parameters
    TransferType m_xferType;                   // Type of transfer in progress
    uint32 m_xferPos;                          // Current transfer position in words
    uint32 m_xferLength;                       // Total number of words to be transferred
    uint32 m_xferCount;                        // Number of words transferred in the last transfer
    std::array<uint16, 2352 / 2> m_xferBuffer; // Transfer buffer
    uint32 m_xferBufferPos;                    // Transfer buffer position

    // Parameters for sector transfers
    uint32 m_xferSectorPos; // Current transfer sector position
    uint32 m_xferSectorEnd; // Last sector to transfer
    uint8 m_xferPartition;  // From which partition to read
    // uint8 m_xferFilter;      // To which filter to write
    uint32 m_xferGetLength; // How many bytes to read from the current sector
    uint32 m_xferDelStart;  // Starting offset of sectors to delete in GetThenDeleteSector transfer
    uint32 m_xferDelCount;  // Number of sectors to delete in GetThenDeleteSector transfer

    // Parameters for subcode transfers
    uint32 m_xferSubcodeFrameAddress; // Last subcode R-W frame address
    uint32 m_xferSubcodeGroup;        // Last subcode R-W group

    // Debugging data
    uint32 m_xferExtraCount; // Number of additional/unexpected reads/writes

    enum class SectorTransferResult { OK, Wait, Reject };

    void SetupTOCTransfer();
    SectorTransferResult SetupGetSectorTransfer(uint16 sectorPos, uint16 sectorCount, uint8 partitionNumber, bool del);
    void SetupPutSectorTransfer(uint16 sectorCount, uint8 partitionNumber);
    uint32 SetupFileInfoTransfer(uint32 fileID);
    bool SetupSubcodeTransfer(uint8 type);

    void ReadSector();

    uint16 DoReadTransfer();
    void DoWriteTransfer(uint16 value);

    void AdvanceTransfer();

    void EndTransfer();

    // -------------------------------------------------------------------------
    // Buffers, partitions and filters
    //
    // The low-level storage unit is the buffer, which stores one sector of 2352 bytes worth of data.
    // The CD block contains 202 buffers, but only 200 are accessible externally.
    //
    // A buffer partition is a logical group of buffers containing a continuous section of data. The partitions are only
    // limited by the total buffer capacity of 200 blocks and can store buffers in any order, much like virtual memory
    // allocations backed by physical memory in systems with MMUs.
    //
    // All streamed data passes through a configurable set of 24 filters that conditionally route data to one of two
    // outputs: "pass" and "fail". There are also 24 buffer partitions used as a staging area for transfers. Every
    // filter and buffer partition has an input and output connector. By default, all filter inputs and buffer partition
    // outputs are disconnected, and filter output connectors are routed to the buffer partition inputs of the same
    // index.
    //
    // The CD block can receive data from these devices that expose an output connector:
    // - The CD drive
    // - The host SH-2 CPU (via writes to the data transfer register on port 0x98000)
    // - The MPEG decoder, which contains the MPEG frame buffer and MPEG sector buffer
    //
    // Data can be streamed out to these devices that expose an input connector:
    // - The host SH-2 CPU (via reads from the data transfer register on port 0x98000)
    // - The MPEG decoder:
    //   - Audio output
    //   - Video output
    //   - Frame buffer (directly connected to the VDP2's EXBG)
    //   - Sector buffer
    //
    // Connections from and to devices are configured by SetCDDeviceConnection, MpegSetConnection, and several transfer
    // commands which make the data accessible by the SH-2 via port 0x98000.
    //
    // Connections are constrained to the following rules:
    // - Output connectors from devices can only be assigned to filter input connectors.
    // - The "pass" output connector of a filter can only be routed to the input connector of a buffer partition.
    //   A buffer partition may receive any number of inputs. Data received from multiple inputs will be concatenated.
    // - The "fail" output connector of a filter can only be assigned to a filter's input connector. The filter may
    //   output data to itself or another filter.
    // - The buffer partition output connector can be assigned to a device input connector or a filter's input connector
    //   through the copy/move commands.
    // - Only one connection can be made to filter input connectors. Attempting to connect another output to a filter
    //   input will sever the existing connection.
    //
    // Disconnected filter output connectors will result in dropping the data.

    class PartitionManager {
    public:
        PartitionManager();

        void UseTracer(debug::ICDBlockTracer *tracer) {
            m_tracer = tracer;
        }

        void Reset();

        uint8 GetBufferCount(uint8 partitionIndex) const;
        uint32 GetFreeBufferCount() const;
        bool ReserveBuffers(uint16 count);
        bool UseReservedBuffers(uint16 count);
        void ReleaseReservedBuffers();

        void InsertHead(uint8 partitionIndex, const Buffer &buffer);
        Buffer *GetTail(uint8 partitionIndex, uint8 offset);
        bool RemoveTail(uint8 partitionIndex, uint8 offset);

        uint32 DeleteSectors(uint8 partitionIndex, uint16 sectorPos, uint16 sectorCount);

        void Clear(uint8 partitionIndex);

        uint32 CalculateSize(uint8 partitionIndex, uint32 start, uint32 end, uint32 getLength) const;

        // -------------------------------------------------------------------------
        // Save states

        void SaveState(savestate::CDBlockSaveState &state) const;
        [[nodiscard]] bool ValidateState(const savestate::CDBlockSaveState &state) const;
        void LoadState(const savestate::CDBlockSaveState &state);

        // -------------------------------------------------------------------------
        // Debugger

        void OnTracerAttached();

    private:
        std::array<std::deque<Buffer>, kNumPartitions> m_partitions;

        uint32 m_freeBuffers;
        uint32 m_reservedBuffers;

        debug::ICDBlockTracer *m_tracer = nullptr;
    };

    PartitionManager m_partitionManager;
    std::array<Filter, kNumFilters> m_filters;

    std::array<Buffer, kNumBuffers + 1> m_scratchBuffers;
    Buffer m_playBuffer;
    uint32 m_scratchBufferPutIndex;

    uint8 m_cdDeviceConnection;
    uint8 m_lastCDWritePartition;

    uint32 m_calculatedPartitionSize;

    uint32 m_getSectorLength;
    uint32 m_putSectorLength;
    uint32 m_putOffset;

    bool ConnectCDDevice(uint8 filterNumber);
    bool DisconnectCDDevice(uint8 filterNumber);

    void DisconnectFilterInput(uint8 filterNumber);

    // -------------------------------------------------------------------------
    // Commands

    bool m_processingCommand; // true if a command being processed

    void SetupCommand();

    void ProcessCommand();

    // General CD block operations
    void CmdGetStatus();          // 0x00
    void CmdGetHardwareInfo();    // 0x01
    void CmdGetTOC();             // 0x02
    void CmdGetSessionInfo();     // 0x03
    void CmdInitializeCDSystem(); // 0x04
    void CmdOpenTray();           // 0x05
    void CmdEndDataTransfer();    // 0x06

    // Basic CD playback operations
    void CmdPlayDisc(); // 0x10
    void CmdSeekDisc(); // 0x11
    void CmdScanDisc(); // 0x12

    // Subcode retrieval
    void CmdGetSubcodeQ_RW(); // 0x20

    // CD-ROM device connection
    void CmdSetCDDeviceConnection(); // 0x30
    void CmdGetCDDeviceConnection(); // 0x31
    void CmdGetLastBufferDest();     // 0x32

    // Filters
    void CmdSetFilterRange();               // 0x40
    void CmdGetFilterRange();               // 0x41
    void CmdSetFilterSubheaderConditions(); // 0x42
    void CmdGetFilterSubheaderConditions(); // 0x43
    void CmdSetFilterMode();                // 0x44
    void CmdGetFilterMode();                // 0x45
    void CmdSetFilterConnection();          // 0x46
    void CmdGetFilterConnection();          // 0x47
    void CmdResetSelector();                // 0x48

    // Buffers and buffer partitions
    void CmdGetBufferSize();       // 0x50
    void CmdGetSectorNumber();     // 0x51
    void CmdCalculateActualSize(); // 0x52
    void CmdGetActualSize();       // 0x53
    void CmdGetSectorInfo();       // 0x54
    void CmdExecuteFADSearch();    // 0x55
    void CmdGetFADSearchResults(); // 0x56

    // Buffer input and output
    void CmdSetSectorLength();         // 0x60
    void CmdGetSectorData();           // 0x61
    void CmdDeleteSectorData();        // 0x62
    void CmdGetThenDeleteSectorData(); // 0x63
    void CmdPutSectorData();           // 0x64
    void CmdCopySectorData();          // 0x65
    void CmdMoveSectorData();          // 0x66
    void CmdGetCopyError();            // 0x67

    // File system operations
    void CmdChangeDirectory();    // 0x70
    void CmdReadDirectory();      // 0x71
    void CmdGetFileSystemScope(); // 0x72
    void CmdGetFileInfo();        // 0x73
    void CmdReadFile();           // 0x74
    void CmdAbortFile();          // 0x75

    // MPEG decoder
    void CmdMpegGetStatus();         // 0x90
    void CmdMpegGetInterrupt();      // 0x91
    void CmdMpegSetInterruptMask();  // 0x92
    void CmdMpegInit();              // 0x93
    void CmdMpegSetMode();           // 0x94
    void CmdMpegPlay();              // 0x95
    void CmdMpegSetDecodingMethod(); // 0x96

    // MPEG stream
    void CmdMpegSetConnection(); // 0x9A
    void CmdMpegGetConnection(); // 0x9B
    void CmdMpegSetStream();     // 0x9D
    void CmdMpegGetStream();     // 0x9E

    // MPEG display screen
    void CmdMpegDisplay();         // 0xA0
    void CmdMpegSetWindow();       // 0xA1
    void CmdMpegSetBorderColor();  // 0xA2
    void CmdMpegSetFade();         // 0xA3
    void CmdMpegSetVideoEffects(); // 0xA4
    void CmdMpegSetLSI();          // 0xAF

    void CmdAuthenticateDevice();    // 0xE0
    void CmdIsDeviceAuthenticated(); // 0xE1
    void CmdGetMpegROM();            // 0xE2

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const sys::CBClockSpeedChange CbClockSpeedChange =
        util::MakeClassMemberRequiredCallback<&CDBlock::UpdateClockRatios>(this);

    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ICDBlockTracer *tracer) {
        if (m_tracer && m_tracer != tracer) {
            m_tracer->Detach();
        }
        m_tracer = tracer;
        m_partitionManager.UseTracer(tracer);
        m_partitionManager.OnTracerAttached();
    }

    class Probe {
    public:
        Probe(CDBlock &cdblock);

        uint8 GetCurrentStatusCode() const;
        uint32 GetCurrentFrameAddress() const;
        uint8 GetCurrentRepeatCount() const;
        uint8 GetMaxRepeatCount() const;
        uint8 GetCurrentControlADRBits() const;
        uint8 GetCurrentTrack() const;
        uint8 GetCurrentIndex() const;

        uint8 GetReadSpeed() const;

        uint8 GetCDDeviceConnection() const;

        const media::fs::FilesystemEntry *GetFileAtFrameAddress(uint32 fad) const;
        std::string GetPathAtFrameAddress(uint32 fad) const;

        std::span<const Filter, kNumFilters> GetFilters() const;

    private:
        CDBlock &m_cdblock;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    Probe m_probe{*this};
    debug::ICDBlockTracer *m_tracer = nullptr;

    uint8 m_netlinkSCR;
};

} // namespace ymir::cdblock
