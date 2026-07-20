#include "wrapper/ext/surround.h"

#include <cstdio>

#include "wrapper/ext/audio_ports.h"
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
    // surround.h: channel_map_capacity must be >= the port's channel count
    // (SR01). The declared count comes from the plugin's own audio-ports
    // provider; tolerated — the map is truncated to the capacity.
    if (auto* ports =
            Plugin::from(p)->extensionProvider<AudioPortsProvider>(CLAP_EXT_AUDIO_PORTS)) {
        clap_audio_port_info info{};
        if (ports->audioPortInfo(portIndex, isInput, &info) &&
            channelMapCapacity < info.channel_count) {
            char detail[128];
            std::snprintf(detail, sizeof(detail),
                          "channel_map_capacity=%u is smaller than the port's channel count %u",
                          channelMapCapacity, info.channel_count);
            Plugin::from(p)->contract().report(Violation::SR01, "surround.get_channel_map()",
                                               detail);
        }
    }
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
