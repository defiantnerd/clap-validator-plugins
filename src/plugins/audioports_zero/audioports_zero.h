#pragma once

#include <atomic>

#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/state.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator AudioPortsZero — implements clap.audio-ports but reports ZERO
// ports in both directions (the counterpart to NoteFX, which omits the
// extension entirely). Hosts must handle a portless plugin that still runs
// a process loop.
//
// Extension profile: audio-ports (0 in / 0 out), params (1), state.
class AudioPortsZeroPlugin final : public Plugin,
                                   public ext::AudioPortsProvider,
                                   public ext::ParamsProvider,
                                   public ext::StateProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit AudioPortsZeroPlugin(const clap_host* host);

protected:
    clap_process_status process(const clap_process* process) noexcept override;

    // ext::AudioPortsProvider
    uint32_t audioPortCount(bool isInput) noexcept override;
    bool audioPortInfo(uint32_t index, bool isInput, clap_audio_port_info* info) noexcept override;

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
        kParamDummy = 0,
    };

    std::atomic<double> _dummy{0.5};
};

} // namespace cvp
