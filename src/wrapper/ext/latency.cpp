#include "wrapper/ext/latency.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

uint32_t sGet(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    auto& contract = Plugin::from(p)->contract();
    // [main-thread & (being-activated | active)] per latency.h — a query on a
    // deactivated plugin reads a value that is allowed to change (LT01).
    // Tolerated: the current latency is returned anyway.
    if (!contract.isActive() && !contract.isBeingActivated())
        contract.report(Violation::LT01, "latency.get()",
                        "called while neither active nor being activated");
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
