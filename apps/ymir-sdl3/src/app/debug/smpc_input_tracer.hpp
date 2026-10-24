#pragma once

#include <ymir/hw/smpc/peripheral/peripheral_report.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <string_view>
#include <vector>

namespace app {

/// Captures resolved SMPC peripheral reports for emulation-frame debug dumps.
struct SMPCInputTracer {
    static constexpr uint64 kUnknownVDP1Frame = std::numeric_limits<uint64>::max();

    struct Entry {
        uint64 emulationFrame;
        uint64 vdp1Frame;
        uint64 pollOrdinal;
        uint8 port;
        ymir::peripheral::PeripheralType type;
        std::array<sint32, 8> values{};
        uint8 valueCount = 0;
    };

    struct Snapshot {
        std::vector<Entry> entries;
        uint64 total = 0;
        uint64 dropped = 0;
    };

    static constexpr size_t kCapacity = 65'536;

    std::atomic_bool enabled{false};

    void Record(uint64 emulationFrame, uint8 port, const ymir::peripheral::PeripheralReport &report) {
        if (!enabled.load(std::memory_order_relaxed)) {
            return;
        }

        Entry entry{
            .emulationFrame = emulationFrame,
            .vdp1Frame = kUnknownVDP1Frame,
            .port = port,
            .type = report.type,
        };
        using ymir::peripheral::PeripheralType;
        switch (report.type) {
        case PeripheralType::ControlPad: SetValues(entry, static_cast<uint16>(report.report.controlPad.buttons)); break;
        case PeripheralType::AnalogPad:
            SetValues(entry, static_cast<uint16>(report.report.analogPad.buttons), report.report.analogPad.analog,
                      report.report.analogPad.x, report.report.analogPad.y, report.report.analogPad.l,
                      report.report.analogPad.r);
            break;
        case PeripheralType::ArcadeRacer:
            SetValues(entry, static_cast<uint16>(report.report.arcadeRacer.buttons), report.report.arcadeRacer.wheel);
            break;
        case PeripheralType::MissionStick:
            SetValues(entry, static_cast<uint16>(report.report.missionStick.buttons),
                      report.report.missionStick.sixAxis, report.report.missionStick.x1, report.report.missionStick.y1,
                      report.report.missionStick.z1, report.report.missionStick.x2, report.report.missionStick.y2,
                      report.report.missionStick.z2);
            break;
        case PeripheralType::VirtuaGun:
            SetValues(entry, report.report.virtuaGun.start, report.report.virtuaGun.trigger,
                      report.report.virtuaGun.reload, report.report.virtuaGun.x, report.report.virtuaGun.y);
            break;
        case PeripheralType::ShuttleMouse:
            SetValues(entry, report.report.shuttleMouse.start, report.report.shuttleMouse.left,
                      report.report.shuttleMouse.middle, report.report.shuttleMouse.right, report.report.shuttleMouse.x,
                      report.report.shuttleMouse.y);
            break;
        case PeripheralType::None: break;
        }

        std::lock_guard lock(m_mutex);
        entry.pollOrdinal = ++m_total;
        if (m_entries.size() == kCapacity) {
            m_entries.pop_front();
            ++m_dropped;
        }
        m_entries.push_back(entry);
    }

    [[nodiscard]] Snapshot GetSnapshot() const {
        std::lock_guard lock(m_mutex);
        return {std::vector<Entry>(m_entries.begin(), m_entries.end()), m_total, m_dropped};
    }

    void Clear() {
        std::lock_guard lock(m_mutex);
        m_entries.clear();
        m_total = 0;
        m_dropped = 0;
    }

    [[nodiscard]] static constexpr std::string_view TypeName(ymir::peripheral::PeripheralType type) {
        using ymir::peripheral::PeripheralType;
        switch (type) {
        case PeripheralType::None: return "none";
        case PeripheralType::ControlPad: return "control_pad";
        case PeripheralType::AnalogPad: return "analog_pad";
        case PeripheralType::ArcadeRacer: return "arcade_racer";
        case PeripheralType::MissionStick: return "mission_stick";
        case PeripheralType::VirtuaGun: return "virtua_gun";
        case PeripheralType::ShuttleMouse: return "shuttle_mouse";
        }
        return "unknown";
    }

private:
    template <typename... TValues>
    static void SetValues(Entry &entry, TValues... values) {
        entry.values = {static_cast<sint32>(values)...};
        entry.valueCount = sizeof...(values);
    }

    mutable std::mutex m_mutex;
    std::deque<Entry> m_entries;
    uint64 m_total = 0;
    uint64 m_dropped = 0;
};

} // namespace app
