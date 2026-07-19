#include "wrapper/ext/configurable_audio_ports.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

ConfigurableAudioPortsProvider* provider(const clap_plugin* p) {
    auto* iface = Plugin::from(p)->extensionProvider<ConfigurableAudioPortsProvider>(
        CLAP_EXT_CONFIGURABLE_AUDIO_PORTS);
    if (!iface)
        iface = Plugin::from(p)->extensionProvider<ConfigurableAudioPortsProvider>(
            CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT);
    return iface;
}

bool sCanApply(const clap_plugin* p, const clap_audio_port_configuration_request* requests,
               uint32_t requestCount) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->canApplyConfiguration(requests, requestCount);
}

bool sApply(const clap_plugin* p, const clap_audio_port_configuration_request* requests,
            uint32_t requestCount) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->applyConfiguration(requests, requestCount);
}

const clap_plugin_configurable_audio_ports kVtable = {
    .can_apply_configuration = sCanApply,
    .apply_configuration = sApply,
};

} // namespace

const clap_plugin_configurable_audio_ports* configurableAudioPortsVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
