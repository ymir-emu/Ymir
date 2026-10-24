#include "vdp1_command_trace_window.hpp"

#include <app/events/emu_event_factory.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace app::ui {

VDP1CommandTraceWindow::VDP1CommandTraceWindow(SharedContext &context)
    : VDPWindowBase(context) {
    m_windowConfig.name = "VDP1 command trace";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP1CommandTraceWindow::DrawContents() {
    auto &tracer = m_context.tracers.VDP;
    bool enabled = tracer.enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Capture VDP1 commands", &enabled)) {
        tracer.enabled.store(enabled, std::memory_order_relaxed);
        m_context.EnqueueEvent(events::emu::SetVDPTrace(enabled));
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        tracer.Clear();
    }

    size_t count = 0;
    uint64 dropped = 0;
    {
        std::lock_guard lock(tracer.mutex);
        count = tracer.entries.size();
        dropped = tracer.dropped;
    }
    ImGui::Text("%zu commands captured (%llu oldest entries dropped)", count,
                static_cast<unsigned long long>(dropped));
    if (ImGui::Button("Export CSV") && count != 0) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
#if defined(_WIN32)
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif
        std::ostringstream filename;
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        filename << "vdp1-command-trace-" << std::put_time(&localTime, "%Y%m%d-%H%M%S") << '-'
                 << std::setw(3) << std::setfill('0') << milliseconds << ".csv";
        const auto path = m_context.profile.GetPath(ProfilePath::Dumps) / filename.str();
        std::vector<VDPTracer::Entry> snapshot;
        {
            std::lock_guard lock(tracer.mutex);
            snapshot.assign(tracer.entries.begin(), tracer.entries.end());
        }
        std::error_code error{};
        std::filesystem::create_directories(path.parent_path(), error);
        if (!error) {
            std::ofstream out(path);
            if (!out) {
                ImGui::Text("Could not open CSV for writing: %s", path.string().c_str());
                return;
            }
            out << "frame,index,address";
            for (size_t i = 0; i < 16; ++i) {
                out << ",word" << i;
            }
            out << ",frame_root,local_x,local_y";
            out << '\n';
            for (const auto &entry : snapshot) {
                out << entry.frame << ',' << entry.index << ",0x" << std::hex << entry.address << std::dec;
                for (uint16 word : entry.words) {
                    out << ",0x" << std::hex << word << std::dec;
                }
                out << ",0x" << std::hex << entry.rootAddress << std::dec << ',' << entry.localX << ','
                    << entry.localY;
                out << '\n';
            }
            out.close();
            if (out) {
                ImGui::Text("Saved %s", path.string().c_str());
            } else {
                ImGui::Text("Failed while writing %s", path.string().c_str());
            }
        } else {
            ImGui::Text("Could not create dump directory: %s", error.message().c_str());
        }
    }
}

} // namespace app::ui
