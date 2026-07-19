#pragma once

#include <atomic>

#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/configurable_audio_ports.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/remote_controls.h"
#include "wrapper/ext/state.h"
#include "wrapper/ext/surround.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Surround — surround passthrough effect with host-switchable
// surround layouts (Quad 4.0, 5.1, 7.1) and NO mono/stereo support.
//
// Extension profile: audio-ports (1 main in / 1 main out, CLAP_PORT_SURROUND
// type, channel count follows the active layout), surround,
// configurable-audio-ports, params (Gain, Solo Channel), state,
// remote-controls. Deliberately absent: note-ports, latency, tail,
// audio-ports-config (layout switching goes through configurable-audio-ports
// exclusively).
//
// Host-testing hooks: is_channel_mask_supported() accepts exactly the three
// surround layout masks — mono/stereo masks and configuration requests are
// rejected. The Solo Channel parameter selects a surround channel
// *identifier* (FL/FR/FC/LFE/BL/BR/SL/SR), so soloing FC means front-center
// in every layout — a wrong host channel map is immediately audible.
class SurroundPlugin final : public Plugin,
                             public ext::AudioPortsProvider,
                             public ext::SurroundProvider,
                             public ext::ConfigurableAudioPortsProvider,
                             public ext::ParamsProvider,
                             public ext::StateProvider,
                             public ext::RemoteControlsProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit SurroundPlugin(const clap_host* host);

protected:
    clap_process_status process(const clap_process* process) noexcept override;

    // ext::AudioPortsProvider
    uint32_t audioPortCount(bool isInput) noexcept override;
    bool audioPortInfo(uint32_t index, bool isInput, clap_audio_port_info* info) noexcept override;

    // ext::SurroundProvider
    bool surroundIsChannelMaskSupported(uint64_t channelMask) noexcept override;
    uint32_t surroundChannelMap(bool isInput, uint32_t portIndex, uint8_t* channelMap,
                                uint32_t capacity) noexcept override;

    // ext::ConfigurableAudioPortsProvider
    bool canApplyConfiguration(const clap_audio_port_configuration_request* requests,
                               uint32_t requestCount) noexcept override;
    bool applyConfiguration(const clap_audio_port_configuration_request* requests,
                            uint32_t requestCount) noexcept override;

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
        kParamGain = 0, // dB
        kParamSolo = 1, // 0=Off, 1..8 = surround identifier (FL..SR)
    };

    // Returns the layout index matching the requests, or -1 when the request
    // set is invalid/unsupported (mono/stereo, wrong port, mixed layouts,
    // foreign channel map).
    int layoutForRequests(const clap_audio_port_configuration_request* requests,
                          uint32_t requestCount) noexcept;

    void applyParamEvent(const clap_event_header* header) noexcept;
    std::atomic<double>* paramStorage(clap_id paramId) noexcept;

    std::atomic<double> _gainDb{0.0};
    std::atomic<double> _solo{0.0};
    std::atomic<uint32_t> _layoutIndex{1}; // default 5.1; written [main & !active] only
};

} // namespace cvp
