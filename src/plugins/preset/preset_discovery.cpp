#include "plugins/preset/preset_discovery.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "plugins/preset/preset_shared.h"
#include "wrapper/earlylog.h"

namespace cvp {

namespace {

// PD01: the indexer must not call back into the provider before init() has
// run, nor re-enter it from within a declare_*() call (preset-discovery.h).
// There is no plugin instance here, so findings go to the EarlyLog (stderr +
// every later plugin instance's log).
void reportProviderMisuse(const char* function, const char* detail) {
    char line[224];
    std::snprintf(line, sizeof(line), "seq [PD01] %s: %s", function, detail);
    earlylog::append(CLAP_LOG_HOST_MISBEHAVING, line);
}

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
    bool initialized = false; // set once init() completed
    bool inDeclare = false;   // set around declare_*() calls into the indexer
};

Provider* self(const clap_preset_discovery_provider* provider) {
    return static_cast<Provider*>(provider->provider_data);
}

bool providerInit(const clap_preset_discovery_provider* provider) {
    auto* p = self(provider);
    if (p->initialized) {
        reportProviderMisuse("provider.init()", "called twice on the same provider");
        return true;
    }

    // Make sure hosts have real files to crawl (idempotent, never overwrites).
    const bool fileLocationUsable = preset::ensureSamplePresetFiles();
    p->fileLocation = preset::presetDirectoryLocation();

    const clap_preset_discovery_filetype filetype = {
        .name = "CVP Preset",
        .description = "clap-validator-plugin preset file",
        .file_extension = preset::kFileExtension,
    };
    p->inDeclare = true;
    const bool filetypeAccepted = p->indexer->declare_filetype(p->indexer, &filetype);
    p->inDeclare = false;
    if (!filetypeAccepted)
        return false;

    // clap-validator <= 0.3.2 cannot handle ANY Windows FILE location: its
    // indexer requires a leading '/', but its crawler then uses the string
    // verbatim, which Windows rejects — and every declaration error fails
    // provider creation. Skip the FILE location for that indexer only; the
    // internal presets keep its tests covered, and real hosts get the
    // spec-conforming plain path.
    bool skipFileLocation = false;
#if defined(_WIN32)
    skipFileLocation = p->indexer->name && std::strcmp(p->indexer->name, "clap-validator") == 0;
#endif

    // Only declare the FILE location when the directory actually exists —
    // declaring an uncrawlable location breaks host indexers (and this can
    // legitimately happen in sandboxed hosts). A rejected FILE location is
    // not fatal either: the internal presets remain available.
    if (fileLocationUsable && !skipFileLocation) {
        const clap_preset_discovery_location fileLocation = {
            .flags = CLAP_PRESET_DISCOVERY_IS_USER_CONTENT,
            .name = "Validator Preset Files",
            .kind = CLAP_PRESET_DISCOVERY_LOCATION_FILE,
            .location = p->fileLocation.c_str(),
        };
        p->inDeclare = true;
        p->indexer->declare_location(p->indexer, &fileLocation);
        p->inDeclare = false;
    }

    const clap_preset_discovery_location pluginLocation = {
        .flags = CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
        .name = "Validator Internal Presets",
        .kind = CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        .location = nullptr, // must be null for the PLUGIN kind
    };
    p->inDeclare = true;
    const bool accepted = p->indexer->declare_location(p->indexer, &pluginLocation);
    p->inDeclare = false;
    p->initialized = accepted;
    return accepted;
}

void providerDestroy(const clap_preset_discovery_provider* provider) {
    delete self(provider);
}

bool providerGetMetadata(const clap_preset_discovery_provider* provider, uint32_t locationKind,
                         const char* location,
                         const clap_preset_discovery_metadata_receiver* receiver) {
    auto* p = self(provider);
    if (!p->initialized)
        reportProviderMisuse("provider.get_metadata()", "called before provider init()");
    else if (p->inDeclare)
        reportProviderMisuse("provider.get_metadata()",
                             "re-entered from within a declare_*() call");

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
        const std::string path = preset::locationToPath(location);
        if (!preset::parsePresetFile(path.c_str(), &data)) {
            // A file we can't parse (foreign or corrupt) must not fail the
            // whole location crawl: report it and skip gracefully.
            if (receiver->on_error)
                receiver->on_error(receiver, 0,
                                   "skipping: not a valid .cvpreset file");
            return true;
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

const void* providerGetExtension(const clap_preset_discovery_provider* provider, const char*) {
    if (!self(provider)->initialized)
        reportProviderMisuse("provider.get_extension()", "called before provider init()");
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
