#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.preset-load — implement and register with BOTH ids (hosts may query
// the compat id):
//   provideExtension(CLAP_EXT_PRESET_LOAD, presetLoadVtable(),
//                    static_cast<PresetLoadProvider*>(this));
//   provideExtension(CLAP_EXT_PRESET_LOAD_COMPAT, presetLoadVtable(),
//                    static_cast<PresetLoadProvider*>(this));
class PresetLoadProvider {
public:
    virtual ~PresetLoadProvider() = default;

    // On success the plugin must notify clap_host_preset_load->loaded().
    virtual bool presetLoadFromLocation(uint32_t locationKind, const char* location,
                                        const char* loadKey) noexcept = 0; // [main]
};

const clap_plugin_preset_load* presetLoadVtable() noexcept;

} // namespace cvp::ext
