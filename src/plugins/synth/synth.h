#pragma once

#include <atomic>

#include "plugins/common/sine_engine.h"
#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/note_ports.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/state.h"
#include "wrapper/ext/voice_info.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Synth — 8-voice polyphonic sine synthesizer.
//
// Extension profile: note-ports (1 in, 0 out), audio-ports (0 in / 1 stereo out),
// params, state, voice-info. Deliberately absent: latency, tail.
class SynthPlugin final : public Plugin,
                          public ext::AudioPortsProvider,
                          public ext::NotePortsProvider,
                          public ext::ParamsProvider,
                          public ext::StateProvider,
                          public ext::VoiceInfoProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit SynthPlugin(const clap_host* host);

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

    // ext::VoiceInfoProvider
    bool voiceInfo(clap_voice_info* info) noexcept override;

private:
    enum ParamId : clap_id {
        kParamVolume = 0,
    };

    void handleEvent(const clap_event_header* header) noexcept;

    SineEngine _engine;
    std::atomic<double> _volume{0.7};
};

} // namespace cvp
