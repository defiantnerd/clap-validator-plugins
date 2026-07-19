#include "wrapper/ext/render.h"

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
