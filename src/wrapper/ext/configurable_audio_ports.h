#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.configurable-audio-ports — implement and register with BOTH ids:
//   provideExtension(CLAP_EXT_CONFIGURABLE_AUDIO_PORTS, configurableAudioPortsVtable(),
//                    static_cast<ConfigurableAudioPortsProvider*>(this));
//   provideExtension(CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT, configurableAudioPortsVtable(),
//                    static_cast<ConfigurableAudioPortsProvider*>(this));
class ConfigurableAudioPortsProvider {
public:
    virtual ~ConfigurableAudioPortsProvider() = default;

    // Both are [main-thread & !active]; requests apply atomically or not at all.
    virtual bool canApplyConfiguration(const clap_audio_port_configuration_request* requests,
                                       uint32_t requestCount) noexcept = 0;
    virtual bool applyConfiguration(const clap_audio_port_configuration_request* requests,
                                    uint32_t requestCount) noexcept = 0;
};

const clap_plugin_configurable_audio_ports* configurableAudioPortsVtable() noexcept;

} // namespace cvp::ext
