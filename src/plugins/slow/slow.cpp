#include "plugins/slow/slow.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

} // namespace

const clap_plugin_descriptor SlowPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.slow",
    .name = "Validator Slow",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Deliberately slow: state save/load block for 'Slowness' seconds, activate "
                   "blocks 250 ms, latency reports one second of samples. Hosts must not freeze "
                   "or time out.",
    .features = kFeatures,
};

const clap_plugin* SlowPlugin::create(const clap_host* host) {
    return (new SlowPlugin(host))->clapPlugin();
}

SlowPlugin::SlowPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_LATENCY, ext::latencyVtable(),
                     static_cast<ext::LatencyProvider*>(this));
}

void SlowPlugin::sleepSlowness(const char* what) noexcept {
    const double seconds = _slowness.load(std::memory_order_relaxed);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "slow: blocking %s for %.1f s", what, seconds);
    logToHost(CLAP_LOG_INFO, buf);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0)));
}

bool SlowPlugin::activate(double, uint32_t, uint32_t) noexcept {
    logToHost(CLAP_LOG_INFO, "slow: blocking activate() for 250 ms");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    return true;
}

// ---- audio-ports ----

uint32_t SlowPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool SlowPlugin::audioPortInfo(uint32_t index, bool isInput,
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

// ---- params ----

uint32_t SlowPlugin::paramCount() noexcept {
    return 1;
}

bool SlowPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kParamSlowness;
    std::snprintf(info->name, sizeof(info->name), "Slowness");
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0;
    info->max_value = 5.0;
    info->default_value = 2.0;
    return true;
}

bool SlowPlugin::paramValue(clap_id paramId, double* value) noexcept {
    if (paramId != kParamSlowness)
        return false;
    *value = _slowness.load(std::memory_order_relaxed);
    return true;
}

bool SlowPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                  uint32_t capacity) noexcept {
    if (paramId != kParamSlowness)
        return false;
    std::snprintf(out, capacity, "%.1f s", value);
    return true;
}

bool SlowPlugin::paramTextToValue(clap_id paramId, const char* text, double* value) noexcept {
    if (paramId != kParamSlowness)
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed;
    return true;
}

void SlowPlugin::paramsFlush(const clap_input_events* in, const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const auto* header = in->get(in, i);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID &&
            header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
            if (event->param_id == kParamSlowness)
                _slowness.store(event->value, std::memory_order_relaxed);
        }
    }
}

// ---- state: the slow part ----

bool SlowPlugin::stateSave(const clap_ostream* stream) noexcept {
    sleepSlowness("stateSave()");
    const double slowness = _slowness.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, slowness);
}

bool SlowPlugin::stateLoad(const clap_istream* stream) noexcept {
    sleepSlowness("stateLoad()");
    uint32_t magic = 0, version = 0;
    double slowness = 2.0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, slowness))
        return false;
    _slowness.store(slowness, std::memory_order_relaxed);
    return true;
}

// ---- latency ----

uint32_t SlowPlugin::latency() noexcept {
    return static_cast<uint32_t>(sampleRate()); // a full second
}

// ---- processing ----

clap_process_status SlowPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;
    paramsFlush(process->in_events, process->out_events);

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
