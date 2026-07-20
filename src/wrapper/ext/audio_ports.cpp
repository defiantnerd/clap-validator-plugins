#include "wrapper/ext/audio_ports.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

AudioPortsProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<AudioPortsProvider>(CLAP_EXT_AUDIO_PORTS);
}

// The port scan is only legal while deactivated (audio-ports.h): the layout
// must stay constant during activation, so a scan while active is AP01.
void checkNotActive(const clap_plugin* p, const char* function) {
    auto& contract = Plugin::from(p)->contract();
    if (contract.isActive() && !contract.isBeingActivated())
        contract.report(Violation::AP01, function,
                        "port scan while active (only legal when deactivated)");
}

uint32_t sCount(const clap_plugin* p, bool isInput) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    checkNotActive(p, "audio_ports.count()");
    return provider(p)->audioPortCount(isInput);
}

bool sGet(const clap_plugin* p, uint32_t index, bool isInput, clap_audio_port_info* info) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    checkNotActive(p, "audio_ports.get()");
    return provider(p)->audioPortInfo(index, isInput, info);
}

const clap_plugin_audio_ports kVtable = {
    .count = sCount,
    .get = sGet,
};

} // namespace

const clap_plugin_audio_ports* audioPortsVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
