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
