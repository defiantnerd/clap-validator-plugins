// CLAP entry point and plugin factory, backed by the registry in registry.cpp.

#include <clap/clap.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "plugins/preset/preset_discovery.h"
#include "registry.h"
#include "wrapper/earlylog.h"

namespace {

// DSO-level contract state (E01..E04). Entry calls may arrive on any thread,
// so overlap can only be detected with counters, not thread identity. All of
// this is best-effort: a racing host is exactly what it tries to expose.
std::atomic<int> gInitCount{0};     // successful init() minus deinit()
std::atomic<int> gEntryCallDepth{0}; // >0 while init()/deinit() runs
std::atomic<int> gOtherCallDepth{0}; // >0 while any other DSO symbol runs

void reportEntry(const char* code, const char* function, const char* detail) {
    char line[256];
    std::snprintf(line, sizeof(line), "seq [%s] %s: %s", code, function, detail);
    cvp::earlylog::append(CLAP_LOG_HOST_MISBEHAVING, line);
}

// E04 check + depth accounting for non-entry symbols (get_factory and the
// factory methods, which are [thread-safe] amongst themselves but must never
// overlap init()/deinit()).
struct OtherCallScope {
    explicit OtherCallScope(const char* function) {
        gOtherCallDepth.fetch_add(1, std::memory_order_acq_rel);
        if (gEntryCallDepth.load(std::memory_order_acquire) > 0)
            reportEntry("E04", function, "called concurrently with entry.init()/deinit()");
    }
    ~OtherCallScope() { gOtherCallDepth.fetch_sub(1, std::memory_order_acq_rel); }
};

void checkInitialized(const char* function) {
    if (gInitCount.load(std::memory_order_acquire) == 0)
        reportEntry("E01", function, "called before entry.init() (or after deinit())");
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) {
    OtherCallScope scope("plugin_factory.get_plugin_count()");
    checkInitialized("plugin_factory.get_plugin_count()");
    uint32_t count = 0;
    cvp::registryEntries(&count);
    return count;
}

const clap_plugin_descriptor* factoryGetPluginDescriptor(const clap_plugin_factory*,
                                                         uint32_t index) {
    OtherCallScope scope("plugin_factory.get_plugin_descriptor()");
    checkInitialized("plugin_factory.get_plugin_descriptor()");
    uint32_t count = 0;
    const auto* entries = cvp::registryEntries(&count);
    return index < count ? entries[index].descriptor : nullptr;
}

const clap_plugin* factoryCreatePlugin(const clap_plugin_factory*, const clap_host* host,
                                       const char* pluginId) {
    OtherCallScope scope("plugin_factory.create_plugin()");
    checkInitialized("plugin_factory.create_plugin()");
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
    if (gEntryCallDepth.fetch_add(1, std::memory_order_acq_rel) > 0)
        reportEntry("E04", "entry.init()", "called concurrently with another entry call");
    if (gOtherCallDepth.load(std::memory_order_acquire) > 0)
        reportEntry("E04", "entry.init()", "called concurrently with another CLAP symbol");
    const int previous = gInitCount.fetch_add(1, std::memory_order_acq_rel);
    if (previous > 0) {
        char detail[96];
        std::snprintf(detail, sizeof(detail),
                      "called again without a matching deinit() (count is now %d)", previous + 1);
        reportEntry("E02", "entry.init()", detail);
    }
    gEntryCallDepth.fetch_sub(1, std::memory_order_acq_rel);
    return true; // spec >= 1.2.0: tolerate double-init defensively (ref-counted)
}

void entryDeinit() {
    if (gEntryCallDepth.fetch_add(1, std::memory_order_acq_rel) > 0)
        reportEntry("E04", "entry.deinit()", "called concurrently with another entry call");
    if (gOtherCallDepth.load(std::memory_order_acquire) > 0)
        reportEntry("E04", "entry.deinit()", "called concurrently with another CLAP symbol");
    if (gInitCount.load(std::memory_order_acquire) == 0)
        reportEntry("E03", "entry.deinit()", "called without a matching successful init()");
    else
        gInitCount.fetch_sub(1, std::memory_order_acq_rel);
    gEntryCallDepth.fetch_sub(1, std::memory_order_acq_rel);
}

const void* entryGetFactory(const char* factoryId) {
    OtherCallScope scope("entry.get_factory()");
    char function[128];
    std::snprintf(function, sizeof(function), "entry.get_factory(\"%s\")",
                  factoryId ? factoryId : "null");
    checkInitialized(function);
    if (!factoryId)
        return nullptr;
    // E01 is tolerated: return the factory anyway (defensive, spec >= 1.2.0).
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
