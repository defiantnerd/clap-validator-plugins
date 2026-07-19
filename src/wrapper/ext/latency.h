#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.latency — implement and register with
// provideExtension(CLAP_EXT_LATENCY, latencyVtable(), static_cast<LatencyProvider*>(this)).
class LatencyProvider {
public:
    virtual ~LatencyProvider() = default;

    virtual uint32_t latency() noexcept = 0; // [main] (only useful while activated)
};

const clap_plugin_latency* latencyVtable() noexcept;

} // namespace cvp::ext
