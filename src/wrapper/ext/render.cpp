#include "wrapper/ext/render.h"

#include <cstdio>

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

RenderProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<RenderProvider>(CLAP_EXT_RENDER);
}

bool sHasHardRealtimeRequirement(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->renderHasHardRealtimeRequirement();
}

bool sSet(const clap_plugin* p, clap_plugin_render_mode mode) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    // Note: render.h has no [!active] predicate on set() — only the mode
    // value itself can be wrong (R01).
    if (mode != CLAP_RENDER_REALTIME && mode != CLAP_RENDER_OFFLINE) {
        char detail[64];
        std::snprintf(detail, sizeof(detail), "unknown render mode %d", static_cast<int>(mode));
        Plugin::from(p)->contract().report(Violation::R01, "render.set()", detail);
        return false;
    }
    return provider(p)->renderSet(mode);
}

const clap_plugin_render kVtable = {
    .has_hard_realtime_requirement = sHasHardRealtimeRequirement,
    .set = sSet,
};

} // namespace

const clap_plugin_render* renderVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
