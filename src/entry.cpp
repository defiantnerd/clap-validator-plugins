// CLAP entry point and plugin factory.
// M0 skeleton: factory exposes zero plugins; the registry lands in M1.

#include <clap/clap.h>

#include <cstring>

namespace {

uint32_t factoryGetPluginCount(const clap_plugin_factory*) {
    return 0;
}

const clap_plugin_descriptor* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t) {
    return nullptr;
}

const clap_plugin* factoryCreatePlugin(const clap_plugin_factory*, const clap_host* host,
                                       const char*) {
    if (!host || !clap_version_is_compatible(host->clap_version))
        return nullptr;
    return nullptr;
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
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = entryInit,
    .deinit = entryDeinit,
    .get_factory = entryGetFactory,
};
