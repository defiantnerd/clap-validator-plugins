#pragma once

#include <clap/clap.h>

#include <vector>

#include "wrapper/threadcheck.h"

namespace cvp {

// Bridges the clap_plugin C vtable to C++ virtuals and owns the extension
// exposure table. A subclass registers exactly the extensions it wants to
// expose via provideExtension() in its constructor; get_extension() returns
// only what was registered, so deliberate omission is the default.
class Plugin {
public:
    Plugin(const clap_plugin_descriptor* descriptor, const clap_host* host);
    virtual ~Plugin() = default;

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    const clap_plugin* clapPlugin() const noexcept { return &_plugin; }

    static Plugin* from(const clap_plugin* plugin) noexcept {
        return static_cast<Plugin*>(plugin->plugin_data);
    }

    const clap_host* host() const noexcept { return _host; }
    ThreadChecker& threadChecker() noexcept { return _threads; }

    // Looks up the provider interface registered for an extension id.
    // Trampolines may assume it exists: the host can only obtain an extension
    // vtable through get_extension(), which only returns registered entries.
    template <typename Iface>
    Iface* extensionProvider(const char* extensionId) const noexcept {
        return static_cast<Iface*>(findProvider(extensionId));
    }

protected:
    // clap_plugin lifecycle, 1:1. Thread contract in brackets.
    virtual bool init() noexcept { return true; }                                            // [main]
    virtual bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames) noexcept { // [main]
        (void)sampleRate;
        (void)minFrames;
        (void)maxFrames;
        return true;
    }
    virtual void deactivate() noexcept {}                                        // [main]
    virtual bool startProcessing() noexcept { return true; }                     // [audio]
    virtual void stopProcessing() noexcept {}                                    // [audio]
    virtual void reset() noexcept {}                                             // [audio]
    virtual clap_process_status process(const clap_process* process) noexcept = 0; // [audio]
    virtual void onMainThread() noexcept {}                                      // [main]

    // Registers an extension for exposure. Call from the subclass constructor
    // only; the table is immutable afterwards. providerIface must be `this`
    // cast to the matching Provider interface (resolves the multiple-
    // inheritance pointer adjustment once, at construction).
    void provideExtension(const char* extensionId, const void* vtable, void* providerIface) noexcept;

    bool isActive() const noexcept { return _active; }
    bool isProcessing() const noexcept { return _processing; }
    double sampleRate() const noexcept { return _sampleRate; }

    // Logs through the host's clap.log extension if present, stderr otherwise.
    void logToHost(clap_log_severity severity, const char* message) const noexcept;

private:
    struct ExtRecord {
        const char* id;
        const void* vtable;
        void* iface;
    };

    void* findProvider(const char* extensionId) const noexcept;
    const void* findVtable(const char* extensionId) const noexcept;

    static bool sInit(const clap_plugin* p);
    static void sDestroy(const clap_plugin* p);
    static bool sActivate(const clap_plugin* p, double sampleRate, uint32_t minFrames,
                          uint32_t maxFrames);
    static void sDeactivate(const clap_plugin* p);
    static bool sStartProcessing(const clap_plugin* p);
    static void sStopProcessing(const clap_plugin* p);
    static void sReset(const clap_plugin* p);
    static clap_process_status sProcess(const clap_plugin* p, const clap_process* process);
    static const void* sGetExtension(const clap_plugin* p, const char* id);
    static void sOnMainThread(const clap_plugin* p);

    clap_plugin _plugin{};
    const clap_host* _host = nullptr;
    const clap_host_log* _hostLog = nullptr;
    std::vector<ExtRecord> _extensions;
    ThreadChecker _threads;
    bool _active = false;
    bool _processing = false;
    double _sampleRate = 0.0;
};

} // namespace cvp
