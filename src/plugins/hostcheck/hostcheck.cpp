#include "plugins/hostcheck/hostcheck.h"

#include <cstdio>
#include <cstring>

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

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

const clap_plugin_descriptor HostCheckPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.hostcheck",
    .name = "Validator HostCheck",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugin",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugin/issues",
    .version = "0.1.0",
    .description = "Probes the host: logs which host-side extensions are present, exercises "
                   "request_callback(), records every lifecycle transition. Deliberately has "
                   "neither params nor state.",
    .features = kFeatures,
};

const clap_plugin* HostCheckPlugin::create(const clap_host* host) {
    return (new HostCheckPlugin(host))->clapPlugin();
}

HostCheckPlugin::HostCheckPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    // Deliberately NOT: CLAP_EXT_PARAMS, CLAP_EXT_STATE — the first plugin in
    // the suite with neither.
    setLifecycleLogging(true);
}

bool HostCheckPlugin::init() noexcept {
    if (!host())
        return true;

    char buf[192];
    std::snprintf(buf, sizeof(buf), "hostcheck: host '%s' %s (%s), clap %d.%d.%d",
                  host()->name ? host()->name : "?", host()->version ? host()->version : "?",
                  host()->vendor ? host()->vendor : "?", host()->clap_version.major,
                  host()->clap_version.minor, host()->clap_version.revision);
    logToHost(CLAP_LOG_INFO, buf);

    unsigned present = 0;
    for (const char* extId : kProbedHostExtensions) {
        const bool found = host()->get_extension(host(), extId) != nullptr;
        present += found ? 1 : 0;
        std::snprintf(buf, sizeof(buf), "hostcheck: host extension %-28s %s", extId,
                      found ? "PRESENT" : "absent");
        logToHost(CLAP_LOG_INFO, buf);
    }
    std::snprintf(buf, sizeof(buf), "hostcheck: %u of %zu probed host extensions present", present,
                  sizeof(kProbedHostExtensions) / sizeof(kProbedHostExtensions[0]));
    logToHost(CLAP_LOG_INFO, buf);

    logToHost(CLAP_LOG_INFO, "hostcheck: calling host->request_callback()");
    host()->request_callback(host());
    return true;
}

void HostCheckPlugin::onMainThread() noexcept {
    logToHost(CLAP_LOG_INFO, "hostcheck: on_main_thread() arrived (request_callback works)");
}

// ---- audio-ports ----

uint32_t HostCheckPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool HostCheckPlugin::audioPortInfo(uint32_t index, bool isInput,
                                    clap_audio_port_info* info) noexcept {
    if (index != 0)
        return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0;
    return true;
}

// ---- processing ----

clap_process_status HostCheckPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;
    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels =
        in.channel_count < out.channel_count ? in.channel_count : out.channel_count;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (out.data32[ch] != in.data32[ch])
            std::memcpy(out.data32[ch], in.data32[ch], frames * sizeof(float));
    }
    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
