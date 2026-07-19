#pragma once

#include <atomic>

#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/latency.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/render.h"
#include "wrapper/ext/state.h"
#include "wrapper/ext/tail.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Effect — stereo gain effect.
//
// Extension profile: audio-ports, params, state, latency, tail, render.
// Deliberately absent: note-ports.
//
// Host-testing hooks: the Latency parameter is reported through clap.latency
// and triggers host->request_restart() when changed while active — dynamic
// latency is a classic host weak spot.
class EffectPlugin final : public Plugin,
                           public ext::AudioPortsProvider,
                           public ext::ParamsProvider,
                           public ext::StateProvider,
                           public ext::LatencyProvider,
                           public ext::TailProvider,
                           public ext::RenderProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit EffectPlugin(const clap_host* host);

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

    // ext::LatencyProvider / ext::TailProvider
    uint32_t latency() noexcept override;
    uint32_t tail() noexcept override;

    // ext::RenderProvider
    bool renderHasHardRealtimeRequirement() noexcept override;
    bool renderSet(clap_plugin_render_mode mode) noexcept override;

private:
    enum ParamId : clap_id {
        kParamGain = 0,    // dB
        kParamLatency = 1, // samples, stepped
        kParamTail = 2,    // seconds
    };
    static constexpr uint32_t kParamCount = 3;

    void applyParamEvent(const clap_event_header* header) noexcept;
    std::atomic<double>* paramStorage(clap_id paramId) noexcept;

    std::atomic<double> _gainDb{0.0};
    std::atomic<double> _latencySamples{0.0};
    std::atomic<double> _tailSeconds{0.0};
    clap_plugin_render_mode _renderMode = CLAP_RENDER_REALTIME;
};

} // namespace cvp
