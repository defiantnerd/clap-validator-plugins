#include "wrapper/ext/surround.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

SurroundProvider* provider(const clap_plugin* p) {
    auto* iface = Plugin::from(p)->extensionProvider<SurroundProvider>(CLAP_EXT_SURROUND);
    if (!iface)
        iface = Plugin::from(p)->extensionProvider<SurroundProvider>(CLAP_EXT_SURROUND_COMPAT);
    return iface;
}

bool sIsChannelMaskSupported(const clap_plugin* p, uint64_t channelMask) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->surroundIsChannelMaskSupported(channelMask);
}

uint32_t sGetChannelMap(const clap_plugin* p, bool isInput, uint32_t portIndex,
                        uint8_t* channelMap, uint32_t channelMapCapacity) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->surroundChannelMap(isInput, portIndex, channelMap, channelMapCapacity);
}

const clap_plugin_surround kVtable = {
    .is_channel_mask_supported = sIsChannelMaskSupported,
    .get_channel_map = sGetChannelMap,
};

} // namespace

const clap_plugin_surround* surroundVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
