#pragma once

#include <atomic>

#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/preset_load.h"
#include "wrapper/ext/remote_controls.h"
#include "wrapper/ext/state.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Preset — stereo gain effect loadable from presets in BOTH
// location kinds:
//   - PLUGIN (internal/factory presets, addressed by load_key), and
//   - FILE (.cvpreset text files, crawled from the user preset directory).
// The matching preset-discovery provider lives in preset_discovery.cpp and
// is exposed through the entry's second factory.
//
// Extension profile: audio-ports (stereo in/out), params (Gain, Color),
// state, preset-load, remote-controls. Deliberately absent: note-ports.
class PresetPlugin final : public Plugin,
                           public ext::AudioPortsProvider,
                           public ext::ParamsProvider,
                           public ext::StateProvider,
                           public ext::PresetLoadProvider,
                           public ext::RemoteControlsProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit PresetPlugin(const clap_host* host);

protected:
    bool init() noexcept override;
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

    // ext::PresetLoadProvider
    bool presetLoadFromLocation(uint32_t locationKind, const char* location,
                                const char* loadKey) noexcept override;

    // ext::RemoteControlsProvider
    uint32_t remoteControlsPageCount() noexcept override;
    bool remoteControlsPage(uint32_t pageIndex,
                            clap_remote_controls_page* page) noexcept override;

private:
    enum ParamId : clap_id {
        kParamGain = 0,  // dB
        kParamColor = 1, // stepped enum 0..3
    };

    void applyParamEvent(const clap_event_header* header) noexcept;
    std::atomic<double>* paramStorage(clap_id paramId) noexcept;

    std::atomic<double> _gainDb{0.0};
    std::atomic<double> _color{0.0};
    const clap_host_preset_load* _hostPresetLoad = nullptr;
    const clap_host_params* _hostParams = nullptr;
};

} // namespace cvp
