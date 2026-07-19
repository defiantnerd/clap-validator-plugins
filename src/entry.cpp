// CLAP entry point and plugin factory, backed by the registry in registry.cpp.

#include <clap/clap.h>

#include <cstring>

#include "plugins/preset/preset_discovery.h"
#include "registry.h"

namespace {

uint32_t factoryGetPluginCount(const clap_plugin_factory*) {
    uint32_t count = 0;
    cvp::registryEntries(&count);
    return count;
}

const clap_plugin_descriptor* factoryGetPluginDescriptor(const clap_plugin_factory*,
                                                         uint32_t index) {
    uint32_t count = 0;
    const auto* entries = cvp::registryEntries(&count);
    return index < count ? entries[index].descriptor : nullptr;
}

const clap_plugin* factoryCreatePlugin(const clap_plugin_factory*, const clap_host* host,
                                       const char* pluginId) {
    if (!host || !clap_version_is_compatible(host->clap_version))
        return nullptr;
    const auto* entry = cvp::registryFind(pluginId);
    return entry ? entry->create(host) : nullptr;
}

const clap_plugin_factory kFactory = {
    .get_plugin_count = factoryGetPluginCount,
    .get_plugin_descriptor = factoryGetPluginDescriptor,
    .create_plugin = factoryCreatePlugin,
};

bool entryInit(const char* /*pluginPath*/) {
    return true;
}

void entryDeinit() {}

const void* entryGetFactory(const char* factoryId) {
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kFactory;
    if (std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0 ||
        std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return cvp::presetDiscoveryFactory();
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = entryInit,
    .deinit = entryDeinit,
    .get_factory = entryGetFactory,
};
