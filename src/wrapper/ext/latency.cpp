#include "wrapper/ext/latency.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

uint32_t sGet(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return Plugin::from(p)->extensionProvider<LatencyProvider>(CLAP_EXT_LATENCY)->latency();
}

const clap_plugin_latency kVtable = {
    .get = sGet,
};

} // namespace

const clap_plugin_latency* latencyVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
