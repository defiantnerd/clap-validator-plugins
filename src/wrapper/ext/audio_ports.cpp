#include "wrapper/ext/audio_ports.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

AudioPortsProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<AudioPortsProvider>(CLAP_EXT_AUDIO_PORTS);
}

uint32_t sCount(const clap_plugin* p, bool isInput) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->audioPortCount(isInput);
}

bool sGet(const clap_plugin* p, uint32_t index, bool isInput, clap_audio_port_info* info) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
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
