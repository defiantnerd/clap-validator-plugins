#include "plugins/preset/preset_discovery.h"

#include <cstring>
#include <string>

#include "plugins/preset/preset_shared.h"

namespace cvp {

namespace {

const clap_preset_discovery_provider_descriptor kProviderDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.preset-discovery",
    .name = "Validator Preset Provider",
    .vendor = "CLAP Validator Plugin Project",
};

struct Provider {
    clap_preset_discovery_provider provider;
    const clap_preset_discovery_indexer* indexer;
    std::string fileLocation; // keeps the declared location string alive
};

Provider* self(const clap_preset_discovery_provider* provider) {
    return static_cast<Provider*>(provider->provider_data);
}

bool providerInit(const clap_preset_discovery_provider* provider) {
    auto* p = self(provider);

    // Make sure hosts have real files to crawl (idempotent, never overwrites).
    const bool fileLocationUsable = preset::ensureSamplePresetFiles();
    p->fileLocation = preset::presetDirectory();

    const clap_preset_discovery_filetype filetype = {
        .name = "CVP Preset",
        .description = "clap-validator-plugin preset file",
        .file_extension = preset::kFileExtension,
    };
    if (!p->indexer->declare_filetype(p->indexer, &filetype))
        return false;

    // Only declare the FILE location when the directory actually exists —
    // declaring an uncrawlable location breaks host indexers (and this can
    // legitimately happen in sandboxed hosts).
    if (fileLocationUsable) {
        const clap_preset_discovery_location fileLocation = {
            .flags = CLAP_PRESET_DISCOVERY_IS_USER_CONTENT,
            .name = "Validator Preset Files",
            .kind = CLAP_PRESET_DISCOVERY_LOCATION_FILE,
            .location = p->fileLocation.c_str(),
        };
        if (!p->indexer->declare_location(p->indexer, &fileLocation))
            return false;
    }

    const clap_preset_discovery_location pluginLocation = {
        .flags = CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
        .name = "Validator Internal Presets",
        .kind = CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        .location = nullptr, // must be null for the PLUGIN kind
    };
    return p->indexer->declare_location(p->indexer, &pluginLocation);
}

void providerDestroy(const clap_preset_discovery_provider* provider) {
    delete self(provider);
}

bool providerGetMetadata(const clap_preset_discovery_provider*, uint32_t locationKind,
                         const char* location,
                         const clap_preset_discovery_metadata_receiver* receiver) {
    const clap_universal_plugin_id pluginId = {.abi = "clap", .id = preset::kPresetPluginId};

    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
        uint32_t count = 0;
        const auto* presets = preset::internalPresets(&count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!receiver->begin_preset(receiver, presets[i].name, presets[i].loadKey))
                return true; // receiver asked to stop — not an error
            receiver->add_plugin_id(receiver, &pluginId);
            receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
            receiver->set_description(receiver, presets[i].description);
            receiver->add_creator(receiver, "CLAP Validator Plugin Project");
        }
        return true;
    }

    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location) {
        preset::PresetData data{};
        if (!preset::parsePresetFile(location, &data)) {
            if (receiver->on_error)
                receiver->on_error(receiver, 0, "not a valid .cvpreset file");
            return false;
        }
        // One preset per file: a null load_key means "the file is the preset".
        if (receiver->begin_preset(receiver, data.name, nullptr)) {
            receiver->add_plugin_id(receiver, &pluginId);
            receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);
        }
        return true;
    }

    return false;
}

const void* providerGetExtension(const clap_preset_discovery_provider*, const char*) {
    return nullptr;
}

uint32_t factoryCount(const clap_preset_discovery_factory*) {
    return 1;
}

const clap_preset_discovery_provider_descriptor*
factoryGetDescriptor(const clap_preset_discovery_factory*, uint32_t index) {
    return index == 0 ? &kProviderDescriptor : nullptr;
}

const clap_preset_discovery_provider*
factoryCreate(const clap_preset_discovery_factory*, const clap_preset_discovery_indexer* indexer,
              const char* providerId) {
    if (!indexer || !providerId || std::strcmp(providerId, kProviderDescriptor.id) != 0)
        return nullptr;
    if (!clap_version_is_compatible(indexer->clap_version))
        return nullptr;

    auto* p = new Provider{};
    p->indexer = indexer;
    p->provider = {
        .desc = &kProviderDescriptor,
        .provider_data = p,
        .init = providerInit,
        .destroy = providerDestroy,
        .get_metadata = providerGetMetadata,
        .get_extension = providerGetExtension,
    };
    return &p->provider;
}

const clap_preset_discovery_factory kFactory = {
    .count = factoryCount,
    .get_descriptor = factoryGetDescriptor,
    .create = factoryCreate,
};

} // namespace

const clap_preset_discovery_factory* presetDiscoveryFactory() noexcept {
    return &kFactory;
}

} // namespace cvp
