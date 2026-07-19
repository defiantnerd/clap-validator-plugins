#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.render — implement and register with
// provideExtension(CLAP_EXT_RENDER, renderVtable(), static_cast<RenderProvider*>(this)).
class RenderProvider {
public:
    virtual ~RenderProvider() = default;

    virtual bool renderHasHardRealtimeRequirement() noexcept = 0;   // [main]
    virtual bool renderSet(clap_plugin_render_mode mode) noexcept = 0; // [main, !active]
};

const clap_plugin_render* renderVtable() noexcept;

} // namespace cvp::ext
