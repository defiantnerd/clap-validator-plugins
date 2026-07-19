#pragma once

#include <atomic>

#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/latency.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/state.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Slow — deliberately slow plugin.
//
// stateSave/stateLoad block for `Slowness` seconds, activate blocks 250 ms,
// and the reported latency is one full second of samples. Hosts must not
// freeze their UI or time the plugin out.
//
// Extension profile: audio-ports (stereo passthrough), params, state, latency.
class SlowPlugin final : public Plugin,
                         public ext::AudioPortsProvider,
                         public ext::ParamsProvider,
                         public ext::StateProvider,
                         public ext::LatencyProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit SlowPlugin(const clap_host* host);

protected:
    bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames) noexcept override;
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

    // ext::LatencyProvider
    uint32_t latency() noexcept override;

private:
    enum ParamId : clap_id {
        kParamSlowness = 0, // seconds of blocking in state save/load
    };

    void sleepSlowness(const char* what) noexcept;

    std::atomic<double> _slowness{2.0};
};

} // namespace cvp
