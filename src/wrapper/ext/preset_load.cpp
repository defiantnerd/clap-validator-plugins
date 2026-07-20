#include "wrapper/ext/preset_load.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

PresetLoadProvider* provider(const clap_plugin* p) {
    auto* iface = Plugin::from(p)->extensionProvider<PresetLoadProvider>(CLAP_EXT_PRESET_LOAD);
    if (!iface)
        iface =
            Plugin::from(p)->extensionProvider<PresetLoadProvider>(CLAP_EXT_PRESET_LOAD_COMPAT);
    return iface;
}

bool sFromLocation(const clap_plugin* p, uint32_t locationKind, const char* location,
                   const char* loadKey) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    auto& contract = Plugin::from(p)->contract();
    // Location semantics follow the preset-discovery kinds (PL01): a FILE
    // load needs a path; a PLUGIN load must pass location = NULL.
    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && !location) {
        contract.report(Violation::PL01, "preset_load.from_location()",
                        "FILE location kind with a null location");
        return false;
    }
    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && location)
        contract.report(Violation::PL01, "preset_load.from_location()",
                        "PLUGIN location kind with a non-null location (must be null)");
    return provider(p)->presetLoadFromLocation(locationKind, location, loadKey);
}

const clap_plugin_preset_load kVtable = {
    .from_location = sFromLocation,
};

} // namespace

const clap_plugin_preset_load* presetLoadVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
