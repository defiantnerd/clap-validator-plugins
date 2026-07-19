// The explicit table of every plugin shipped in this binary.
// Adding a plugin: create its directory under src/plugins/ and add one line here.

#include "registry.h"

#include <cstring>

#include "plugins/effect/effect.h"

namespace cvp {

namespace {

const PluginEntry kEntries[] = {
    {&EffectPlugin::descriptor, &EffectPlugin::create},
};

} // namespace

const PluginEntry* registryEntries(uint32_t* count) noexcept {
    *count = static_cast<uint32_t>(sizeof(kEntries) / sizeof(kEntries[0]));
    return kEntries;
}

const PluginEntry* registryFind(const char* pluginId) noexcept {
    if (!pluginId)
        return nullptr;
    for (const auto& entry : kEntries)
        if (std::strcmp(entry.descriptor->id, pluginId) == 0)
            return &entry;
    return nullptr;
}

} // namespace cvp
