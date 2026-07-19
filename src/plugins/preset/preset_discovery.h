#pragma once

#include <clap/clap.h>

namespace cvp {

// The preset-discovery factory exposed by clap_entry.get_factory() under
// CLAP_PRESET_DISCOVERY_FACTORY_ID (and its compat id). One provider that
// declares both location kinds: the user .cvpreset file directory and the
// plugin-internal factory presets.
const clap_preset_discovery_factory* presetDiscoveryFactory() noexcept;

} // namespace cvp
