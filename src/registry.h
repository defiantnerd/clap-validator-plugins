#pragma once

#include <clap/clap.h>

#include <cstdint>

namespace cvp {

struct PluginEntry {
    const clap_plugin_descriptor* descriptor;
    // Returns a fully constructed, un-inited plugin. Never null on success.
    const clap_plugin* (*create)(const clap_host* host);
};

const PluginEntry* registryEntries(uint32_t* count) noexcept;
const PluginEntry* registryFind(const char* pluginId) noexcept;

} // namespace cvp
