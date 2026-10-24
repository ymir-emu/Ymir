#include "vdp2_memory_trace_window.hpp"

#include <app/events/emu_event_factory.hpp>

namespace app::ui {

VDP2MemoryTraceWindow::VDP2MemoryTraceWindow(SharedContext &context)
    : VDPWindowBase(context) {
    m_windowConfig.name = "VDP2 memory write trace";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2MemoryTraceWindow::DrawContents() {
    auto &tracer = m_context.tracers.VDP2Memory;
    bool enabled = tracer.enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Capture VDP2 VRAM/CRAM writes", &enabled)) {
        tracer.enabled.store(enabled, std::memory_order_relaxed);
        m_context.EnqueueEvent(events::emu::SetVDP2MemoryTrace(enabled));
    }

    bool traceVRAM = tracer.traceVRAM.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("VRAM", &traceVRAM)) {
        tracer.traceVRAM.store(traceVRAM, std::memory_order_relaxed);
    }
    ImGui::SameLine();
    bool traceCRAM = tracer.traceCRAM.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("CRAM", &traceCRAM)) {
        tracer.traceCRAM.store(traceCRAM, std::memory_order_relaxed);
    }

    uint32 addressBegin = tracer.addressBegin.load(std::memory_order_relaxed);
    uint32 addressEnd = tracer.addressEnd.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputScalar("Address from", ImGuiDataType_U32, &addressBegin, nullptr, nullptr, "%05X", ImGuiInputTextFlags_CharsHexadecimal)) {
        tracer.addressBegin.store(addressBegin, std::memory_order_relaxed);
    }
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputScalar("Address to (exclusive)", ImGuiDataType_U32, &addressEnd, nullptr, nullptr, "%05X", ImGuiInputTextFlags_CharsHexadecimal)) {
        tracer.addressEnd.store(addressEnd, std::memory_order_relaxed);
    }

    size_t count = 0;
    uint64 dropped = 0;
    {
        std::lock_guard lock(tracer.mutex);
        count = tracer.entries.size();
        dropped = tracer.dropped;
    }
    ImGui::Text("%zu writes captured (%llu oldest writes dropped)", count,
                static_cast<unsigned long long>(dropped));

    if (ImGui::Button("Clear")) {
        tracer.Clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Export synchronized bundle") && count != 0) {
        m_context.EnqueueEvent(events::emu::ExportVDP2MemoryTrace());
    }
    ImGui::TextWrapped("Addresses and values are observed writes after VDP2 address mapping. This trace does not assign a CPU/DMA source.");
}

} // namespace app::ui
