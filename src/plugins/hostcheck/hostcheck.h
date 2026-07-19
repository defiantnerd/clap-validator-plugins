#pragma once

#include "wrapper/ext/audio_ports.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator HostCheck — exercises the host and logs everything it does.
//
// Host identity + host-extension probing is logged by the Plugin base for
// every flavor; this one additionally calls host->request_callback() and
// logs the on_main_thread() arrival, and has lifecycle logging enabled so
// every transition the host drives is recorded.
//
// Extension profile: audio-ports (stereo passthrough) ONLY — deliberately
// no params and no state: the first plugin in the suite with neither.
class HostCheckPlugin final : public Plugin, public ext::AudioPortsProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit HostCheckPlugin(const clap_host* host);

protected:
    bool init() noexcept override;
    void onMainThread() noexcept override;
    clap_process_status process(const clap_process* process) noexcept override;

    // ext::AudioPortsProvider
    uint32_t audioPortCount(bool isInput) noexcept override;
    bool audioPortInfo(uint32_t index, bool isInput, clap_audio_port_info* info) noexcept override;
};

} // namespace cvp
