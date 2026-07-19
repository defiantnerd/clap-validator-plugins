#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.audio-ports-config — implement and register with
// provideExtension(CLAP_EXT_AUDIO_PORTS_CONFIG, audioPortsConfigVtable(),
//                  static_cast<AudioPortsConfigProvider*>(this)).
class AudioPortsConfigProvider {
public:
    virtual ~AudioPortsConfigProvider() = default;

    virtual uint32_t audioPortsConfigCount() noexcept = 0; // [main]
    virtual bool audioPortsConfigInfo(uint32_t index,
                                      clap_audio_ports_config* config) noexcept = 0; // [main]
    virtual bool audioPortsConfigSelect(clap_id configId) noexcept = 0; // [main, plugin !active]
};

const clap_plugin_audio_ports_config* audioPortsConfigVtable() noexcept;

} // namespace cvp::ext
