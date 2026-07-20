#pragma once

#include <atomic>

#include "plugins/common/sine_engine.h"
#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/note_name.h"
#include "wrapper/ext/note_ports.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/remote_controls.h"
#include "wrapper/ext/state.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Sidechain Synth — sine synthesizer whose output level is
// modulated by a NON-MAIN stereo audio input ("Sidechain").
//
// Extension profile: note-ports (1 in), audio-ports (1 non-main stereo in +
// 1 main stereo out), params, state, remote-controls. Deliberately absent:
// latency, tail, voice-info.
//
// Host-testing hook: an instrument with a non-main audio input exercises
// hosts' audio-into-instrument routing. Correct routing is audibly
// verifiable — the sidechain signal gates the synth output.
class SidechainSynthPlugin final : public Plugin,
                                   public ext::AudioPortsProvider,
                                   public ext::NoteNameProvider,
                                   public ext::NotePortsProvider,
                                   public ext::ParamsProvider,
                                   public ext::StateProvider,
                                   public ext::RemoteControlsProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit SidechainSynthPlugin(const clap_host* host);

protected:
    bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames) noexcept override;
    void reset() noexcept override;
    clap_process_status process(const clap_process* process) noexcept override;

    // ext::AudioPortsProvider
    uint32_t audioPortCount(bool isInput) noexcept override;
    bool audioPortInfo(uint32_t index, bool isInput, clap_audio_port_info* info) noexcept override;

    // ext::NotePortsProvider
    uint32_t notePortCount(bool isInput) noexcept override;
    bool notePortInfo(uint32_t index, bool isInput, clap_note_port_info* info) noexcept override;

    // ext::NoteNameProvider
    uint32_t noteNameCount() noexcept override;
    bool noteNameGet(uint32_t index, clap_note_name* noteName) noexcept override;

    // ext::ParamsProvider
    uint32_t paramCount() noexcept override;
    bool paramInfo(uint32_t index, clap_param_info* info) noexcept override;
    bool paramValue(clap_id paramId, double* value) noexcept override;
    bool paramValueToText(clap_id paramId, double value, char* out,
                          uint32_t capacity) noexcept override;
    bool paramTextToValue(clap_id paramId, const char* text, double* value) noexcept override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    // ext::StateProvider
    bool stateSave(const clap_ostream* stream) noexcept override;
    bool stateLoad(const clap_istream* stream) noexcept override;

    // ext::RemoteControlsProvider
    uint32_t remoteControlsPageCount() noexcept override;
    bool remoteControlsPage(uint32_t pageIndex,
                            clap_remote_controls_page* page) noexcept override;

private:
    enum ParamId : clap_id {
        kParamVolume = 0,
        kParamSidechainAmount = 1,
    };

    void applyParamEvent(const clap_event_header* header) noexcept;
    void handleEvent(const clap_event_header* header) noexcept;
    std::atomic<double>* paramStorage(clap_id paramId) noexcept;

    SineEngine _engine;
    std::atomic<double> _volume{0.7};
    std::atomic<double> _sidechainAmount{1.0};
    float _envelope = 0.0f; // one-pole follower over the sidechain input, audio thread only
};

} // namespace cvp
