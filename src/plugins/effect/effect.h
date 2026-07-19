#pragma once

#include "wrapper/ext/audio_ports.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator Effect — stereo audio effect.
//
// Extension profile (M1: audio-ports only; params/state/latency/tail/render
// arrive in M2). Deliberately absent: note-ports.
class EffectPlugin final : public Plugin, public ext::AudioPortsProvider {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit EffectPlugin(const clap_host* host);

protected:
    clap_process_status process(const clap_process* process) noexcept override;

    // ext::AudioPortsProvider
    uint32_t audioPortCount(bool isInput) noexcept override;
    bool audioPortInfo(uint32_t index, bool isInput, clap_audio_port_info* info) noexcept override;
};

} // namespace cvp
