#pragma once

#include <atomic>

#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/remote_controls.h"
#include "wrapper/ext/state.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Params — a parameter-type zoo: one parameter per CLAP param flag
// constellation the rest of the suite doesn't cover.
//
//   Gain      automatable + modulatable, and the only param in the suite with
//             a NON-NULL cookie — hosts exercising the cookie fast path get
//             checked (a wrong cookie is reported as P11)
//   Drive     flags = 0: host-accessible via live changes, NOT automatable
//   Peak      read-only output meter, written by process(), polled by the host
//             (a host sending value events for it is reported as P12)
//   Bypass    the one allowed CLAP_PARAM_IS_BYPASS param
//   Rotation  periodic 0..360° stereo rotation (wraps around)
//   DC Offset CLAP_PARAM_REQUIRES_PROCESS — the header's own example
//   Curve     stepped enum (Linear/Soft/Hard drive curve)
//   Secret    hidden: addressable by id, must not be shown to the user
//
// Extension profile: audio-ports (stereo in/out, 32-bit only), params, state,
// remote-controls. Deliberately absent: note-ports, latency, tail, render.
class ParamsZooPlugin final : public Plugin,
                              public ext::AudioPortsProvider,
                              public ext::ParamsProvider,
                              public ext::StateProvider,
                              public ext::RemoteControlsProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit ParamsZooPlugin(const clap_host* host);

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

    // ext::RemoteControlsProvider
    uint32_t remoteControlsPageCount() noexcept override;
    bool remoteControlsPage(uint32_t pageIndex,
                            clap_remote_controls_page* page) noexcept override;

private:
    enum ParamId : clap_id {
        kParamGain = 1,     // dB, automatable + modulatable, non-null cookie
        kParamDrive = 2,    // 0..10, flags = 0 (not automatable)
        kParamPeak = 3,     // 0..1, read-only output meter
        kParamBypass = 4,   // 0/1, bypass
        kParamRotation = 5, // degrees, periodic
        kParamDcOffset = 6, // -0.2..0.2, requires-process
        kParamCurve = 7,    // 0..2 enum: Linear/Soft/Hard
        kParamSecret = 8,   // 0..1 trim, hidden
    };
    static constexpr uint32_t kParamCount = 8;

    void applyParamEvent(const clap_event_header* header) noexcept;
    std::atomic<double>* paramStorage(clap_id paramId) noexcept;

    std::atomic<double> _gainDb{0.0};
    std::atomic<double> _gainModDb{0.0}; // PARAM_MOD offset, never persisted
    std::atomic<double> _drive{0.0};
    std::atomic<double> _peak{0.0};
    std::atomic<double> _bypass{0.0};
    std::atomic<double> _rotationDeg{0.0};
    std::atomic<double> _dcOffset{0.0};
    std::atomic<double> _curve{0.0};
    std::atomic<double> _secretTrim{1.0};
};

} // namespace cvp
