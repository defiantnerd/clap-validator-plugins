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

// Both methods are [main-thread & !active] (configurable-audio-ports.h) —
// CA01 when the host asks while the engine is running.
bool checkNotActive(const clap_plugin* p, const char* function) {
    auto& contract = Plugin::from(p)->contract();
    if (!contract.isActive())
        return true;
    contract.report(Violation::CA01, function,
                    "called while active (only legal when deactivated)");
    return false;
}

bool sCanApply(const clap_plugin* p, const clap_audio_port_configuration_request* requests,
               uint32_t requestCount) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!checkNotActive(p, "configurable_audio_ports.can_apply_configuration()"))
        return false;
    return provider(p)->canApplyConfiguration(requests, requestCount);
}

bool sApply(const clap_plugin* p, const clap_audio_port_configuration_request* requests,
            uint32_t requestCount) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!checkNotActive(p, "configurable_audio_ports.apply_configuration()"))
        return false;
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
