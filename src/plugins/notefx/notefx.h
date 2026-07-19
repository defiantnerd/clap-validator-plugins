#pragma once

#include <atomic>

#include "wrapper/ext/note_ports.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/state.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator NoteFX — pure note transformer (transpose).
//
// Extension profile: note-ports (1 in / 1 out, CLAP+MIDI dialects), params, state.
// Deliberately absent: the clap.audio-ports extension is NOT implemented at
// all (rather than implemented with zero ports) — hosts that assume every
// plugin has audio-ports must cope.
class NoteFxPlugin final : public Plugin,
                           public ext::NotePortsProvider,
                           public ext::ParamsProvider,
                           public ext::StateProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit NoteFxPlugin(const clap_host* host);

protected:
    clap_process_status process(const clap_process* process) noexcept override;

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

private:
    enum ParamId : clap_id {
        kParamTranspose = 0, // semitones, stepped
    };

    void transformEvent(const clap_event_header* header,
                        const clap_output_events* out) noexcept;

    std::atomic<double> _transpose{0.0};
};

} // namespace cvp
