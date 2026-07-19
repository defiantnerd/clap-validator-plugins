// Host identity + host-side extension availability logging, run by every
// plugin instance at init(). Lands in the per-instance LogBuffer (visible in
// GUI log panes) and is forwarded to the host's clap.log / stderr.

#include <cstdio>

#include "wrapper/plugin.h"

namespace cvp {

namespace {

// Host-side extensions worth probing (stable set of clap 1.2.10).
const char* kProbedHostExtensions[] = {
    CLAP_EXT_AMBISONIC,
    CLAP_EXT_AUDIO_PORTS,
    CLAP_EXT_AUDIO_PORTS_CONFIG,
    CLAP_EXT_CONTEXT_MENU,
    CLAP_EXT_EVENT_REGISTRY,
    CLAP_EXT_GUI,
    CLAP_EXT_LATENCY,
    CLAP_EXT_LOG,
    CLAP_EXT_NOTE_NAME,
    CLAP_EXT_NOTE_PORTS,
    CLAP_EXT_PARAMS,
    CLAP_EXT_POSIX_FD_SUPPORT,
    CLAP_EXT_PRESET_LOAD,
    CLAP_EXT_REMOTE_CONTROLS,
    CLAP_EXT_STATE,
    CLAP_EXT_SURROUND,
    CLAP_EXT_TAIL,
    CLAP_EXT_THREAD_CHECK,
    CLAP_EXT_THREAD_POOL,
    CLAP_EXT_TIMER_SUPPORT,
    CLAP_EXT_TRACK_INFO,
    CLAP_EXT_VOICE_INFO,
};

} // namespace

void Plugin::logHostInfo() noexcept {
    if (!_host)
        return;

    char buf[256];
    std::snprintf(buf, sizeof(buf), "host: name='%s' vendor='%s' version='%s' url='%s'",
                  _host->name ? _host->name : "?", _host->vendor ? _host->vendor : "?",
                  _host->version ? _host->version : "?", _host->url ? _host->url : "?");
    logToHost(CLAP_LOG_INFO, buf);
    std::snprintf(buf, sizeof(buf), "host: clap version %d.%d.%d", _host->clap_version.major,
                  _host->clap_version.minor, _host->clap_version.revision);
    logToHost(CLAP_LOG_INFO, buf);

    unsigned present = 0;
    for (const char* extId : kProbedHostExtensions) {
        const bool found = _host->get_extension(_host, extId) != nullptr;
        present += found ? 1 : 0;
        std::snprintf(buf, sizeof(buf), "host: extension %-28s %s", extId,
                      found ? "PRESENT" : "absent");
        logToHost(CLAP_LOG_DEBUG, buf);
    }
    std::snprintf(buf, sizeof(buf), "host: %u of %zu probed host extensions present", present,
                  sizeof(kProbedHostExtensions) / sizeof(kProbedHostExtensions[0]));
    logToHost(CLAP_LOG_INFO, buf);
}

} // namespace cvp
