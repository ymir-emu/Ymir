#include "audio_settings_view.hpp"

#include <app/services/midi_service.hpp>

#include <app/events/emu_event_factory.hpp>

#include <app/ui/i18n.hpp>
#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>

#include <app/imgui_data.hpp>

#include <rtmidi/RtMidi.h>

using namespace ymir;

namespace app::ui {


using MidiPortType = Settings::Audio::MidiPort::Type;

AudioSettingsView::AudioSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void AudioSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings().audio;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("General"));
    ImGui::PopFont();

    static constexpr float kMinVolume = 0.0f;
    static constexpr float kMaxVolume = 100.0f;
    float volumePct = settings.volume * 100.0f;
    if (MakeDirty(ImGui::SliderScalar(_("Volume"), ImGuiDataType_Float, &volumePct, &kMinVolume, &kMaxVolume, "%.1lf%%",
                                      ImGuiSliderFlags_AlwaysClamp))) {
        settings.volume = volumePct * 0.01f;
    }
    bool mute = settings.mute;
    if (MakeDirty(ImGui::Checkbox(_("Mute"), &mute))) {
        settings.mute = mute;
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Quality"));
    ImGui::PopFont();

    widgets::settings::audio::InterpolationMode(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("MIDI");
    ImGui::PopFont();

    using MidiPortType = Settings::Audio::MidiPort::Type;

    auto &midiService = m_context.serviceLocator.GetRequired<services::MIDIService>();
    bool supportsVirtual = false;
    auto api = midiService.GetInput()->getCurrentApi();
    if (api == RtMidi::Api::MACOSX_CORE || api == RtMidi::Api::LINUX_ALSA || api == RtMidi::Api::UNIX_JACK) {
        supportsVirtual = true;
    }

    ImGui::Text("Using %s MIDI API.", RtMidi::getApiDisplayName(api).c_str());

    // INPUT PORTS

    auto midiInput = midiService.GetInput();
    const std::string inputPortName = midiService.GetMidiInputPortName();
    const std::string inputLabel = fmt::format("Input port {}", midiInput->isPortOpen() ? "(open)" : "");

    auto inputPort = settings.midiInputPort.Get();

    if (ImGui::BeginCombo(inputLabel.c_str(), inputPortName.c_str())) {
        if (MakeDirty(ImGui::Selectable(_("None"), inputPort.type == MidiPortType::None))) {
            settings.midiInputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::None};
        }

        int portCount = midiInput->getPortCount();
        for (int i = 0; i < portCount; i++) {
            std::string portName = midiInput->getPortName(i);
            bool selected = inputPort.type == MidiPortType::Normal && inputPort.id == portName;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiInputPort = Settings::Audio::MidiPort{.id = portName, .type = MidiPortType::Normal};
            }
        }

        // if the backend supports virtual MIDI ports, show a virtual port also
        if (supportsVirtual) {
            const std::string portName = midiService.GetMidiVirtualInputPortName();
            bool selected = inputPort.type == MidiPortType::Virtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiInputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::Virtual};
            }
        }

        ImGui::EndCombo();
    }

    // OUTPUT PORTS

    auto midiOutput = midiService.GetOutput();
    const std::string outputPortName = midiService.GetMidiOutputPortName();
    const std::string outputLabel = fmt::format("Output port {}", midiOutput->isPortOpen() ? "(open)" : "");

    auto outputPort = settings.midiOutputPort.Get();

    if (ImGui::BeginCombo(outputLabel.c_str(), outputPortName.c_str())) {
        if (MakeDirty(ImGui::Selectable(_("None"), outputPort.type == MidiPortType::None))) {
            settings.midiOutputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::None};
        }

        int portCount = midiOutput->getPortCount();
        for (int i = 0; i < portCount; i++) {
            std::string portName = midiOutput->getPortName(i);
            bool selected = outputPort.type == MidiPortType::Normal && outputPort.id == portName;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiOutputPort = Settings::Audio::MidiPort{.id = portName, .type = MidiPortType::Normal};
            }
        }

        // if the backend supports virtual MIDI ports, show a virtual port also
        if (supportsVirtual) {
            const std::string portName = midiService.GetMidiVirtualOutputPortName();
            bool selected = outputPort.type == MidiPortType::Virtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiOutputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::Virtual};
            }
        }

        ImGui::EndCombo();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Accuracy"));
    ImGui::PopFont();

    widgets::settings::audio::StepGranularity(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Performance"));
    ImGui::PopFont();

    widgets::settings::audio::ThreadedSCSP(m_context);
}

} // namespace app::ui
