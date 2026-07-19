#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.audio-ports — implement and register with
// provideExtension(CLAP_EXT_AUDIO_PORTS, audioPortsVtable(), static_cast<AudioPortsProvider*>(this)).
class AudioPortsProvider {
public:
    virtual ~AudioPortsProvider() = default;

    virtual uint32_t audioPortCount(bool isInput) noexcept = 0; // [main]
    virtual bool audioPortInfo(uint32_t index, bool isInput,
                               clap_audio_port_info* info) noexcept = 0; // [main]
};

const clap_plugin_audio_ports* audioPortsVtable() noexcept;

} // namespace cvp::ext
