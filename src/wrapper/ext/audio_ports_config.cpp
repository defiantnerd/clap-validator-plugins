#include "wrapper/ext/audio_ports_config.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

AudioPortsConfigProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<AudioPortsConfigProvider>(
        CLAP_EXT_AUDIO_PORTS_CONFIG);
}

uint32_t sCount(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->audioPortsConfigCount();
}

bool sGet(const clap_plugin* p, uint32_t index, clap_audio_ports_config* config) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->audioPortsConfigInfo(index, config);
}

bool sSelect(const clap_plugin* p, clap_id configId) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->audioPortsConfigSelect(configId);
}

const clap_plugin_audio_ports_config kVtable = {
    .count = sCount,
    .get = sGet,
    .select = sSelect,
};

} // namespace

const clap_plugin_audio_ports_config* audioPortsConfigVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
