#pragma once

#include <atomic>

#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/audio_ports_config.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/state.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator MultiOut FX — stereo effect fanning its input out to multiple
// output ports, with switchable output configurations.
//
// Extension profile: audio-ports (1 stereo main in; main out + 2 stereo aux
// outs in the "Multi Out" config), audio-ports-config, params, state.
// Deliberately absent: note-ports.
//
// Host-testing hook: the main out keeps port id 0 in both configs (stable
// port ids across configs).
class MultiOutFxPlugin final : public Plugin,
                               public ext::AudioPortsProvider,
                               public ext::AudioPortsConfigProvider,
                               public ext::ParamsProvider,
                               public ext::StateProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit MultiOutFxPlugin(const clap_host* host);

protected:
    clap_process_status process(const clap_process* process) noexcept override;

    // ext::AudioPortsProvider
    uint32_t audioPortCount(bool isInput) noexcept override;
    bool audioPortInfo(uint32_t index, bool isInput, clap_audio_port_info* info) noexcept override;

    // ext::AudioPortsConfigProvider
    uint32_t audioPortsConfigCount() noexcept override;
    bool audioPortsConfigInfo(uint32_t index, clap_audio_ports_config* config) noexcept override;
    bool audioPortsConfigSelect(clap_id configId) noexcept override;

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
        kParamAuxLevel = 0,
    };
    enum ConfigId : clap_id {
        kConfigStereo = 0,
        kConfigMultiOut = 1,
    };
    static constexpr uint32_t kMaxOutPorts = 3; // main + 2 aux

    uint32_t currentOutPortCount() const noexcept;
    void applyParamEvent(const clap_event_header* header) noexcept;

    std::atomic<double> _auxLevel{1.0};
    clap_id _config = kConfigStereo; // [main] while inactive only
};

} // namespace cvp
