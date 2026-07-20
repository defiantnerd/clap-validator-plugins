#include "plugins/effect/effect.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

} // namespace

const clap_plugin_descriptor EffectPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.effect",
    .name = "Validator Effect",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Stereo gain effect exposing audio-ports, params, state, latency, tail and "
                   "render — and deliberately no note-ports. Both ports support 64-bit "
                   "processing but require a common sample size. The Latency parameter changes "
                   "the reported latency at runtime.",
    .features = kFeatures,
};

const clap_plugin* EffectPlugin::create(const clap_host* host) {
    return (new EffectPlugin(host))->clapPlugin();
}

EffectPlugin::EffectPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_LATENCY, ext::latencyVtable(),
                     static_cast<ext::LatencyProvider*>(this));
    provideExtension(CLAP_EXT_TAIL, ext::tailVtable(), static_cast<ext::TailProvider*>(this));
    provideExtension(CLAP_EXT_RENDER, ext::renderVtable(), static_cast<ext::RenderProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS_COMPAT, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    // Deliberately NOT: CLAP_EXT_NOTE_PORTS.
}

// ---- audio-ports ----

uint32_t EffectPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool EffectPlugin::audioPortInfo(uint32_t index, bool isInput,
                                 clap_audio_port_info* info) noexcept {
    if (index != 0)
        return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
    // Both sample sizes are accepted, but in and out must use the same one —
    // a host mixing them is reported as P10 (and tolerated with conversion).
    info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS |
                  CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0;
    return true;
}

// ---- params ----

std::atomic<double>* EffectPlugin::paramStorage(clap_id paramId) noexcept {
    switch (paramId) {
    case kParamGain:
        return &_gainDb;
    case kParamLatency:
        return &_latencySamples;
    case kParamTail:
        return &_tailSeconds;
    default:
        return nullptr;
    }
}

uint32_t EffectPlugin::paramCount() noexcept {
    return kParamCount;
}

bool EffectPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index >= kParamCount)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->cookie = nullptr;
    std::snprintf(info->module, sizeof(info->module), "%s", "");
    switch (index) {
    case 0:
        info->id = kParamGain;
        std::snprintf(info->name, sizeof(info->name), "Gain");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = -36.0;
        info->max_value = 36.0;
        info->default_value = 0.0;
        break;
    case 1:
        info->id = kParamLatency;
        std::snprintf(info->name, sizeof(info->name), "Latency");
        // Stepped, not automatable: a latency change forces a restart.
        info->flags = CLAP_PARAM_IS_STEPPED;
        info->min_value = 0.0;
        info->max_value = 8192.0;
        info->default_value = 0.0;
        break;
    case 2:
        info->id = kParamTail;
        std::snprintf(info->name, sizeof(info->name), "Tail");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0.0;
        info->max_value = 10.0;
        info->default_value = 0.0;
        break;
    }
    return true;
}

bool EffectPlugin::paramValue(clap_id paramId, double* value) noexcept {
    const auto* storage = paramStorage(paramId);
    if (!storage)
        return false;
    *value = storage->load(std::memory_order_relaxed);
    return true;
}

bool EffectPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                    uint32_t capacity) noexcept {
    switch (paramId) {
    case kParamGain:
        std::snprintf(out, capacity, "%.1f dB", value);
        return true;
    case kParamLatency:
        std::snprintf(out, capacity, "%d smp", static_cast<int>(value));
        return true;
    case kParamTail:
        std::snprintf(out, capacity, "%.2f s", value);
        return true;
    default:
        return false;
    }
}

bool EffectPlugin::paramTextToValue(clap_id paramId, const char* text, double* value) noexcept {
    if (!paramStorage(paramId))
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed;
    return true;
}

void EffectPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    auto* storage = paramStorage(event->param_id);
    if (!storage)
        return;
    const double previous = storage->exchange(event->value, std::memory_order_relaxed);
    if (event->param_id == kParamLatency && previous != event->value && isActive() && host())
        host()->request_restart(host()); // [thread-safe]
}

void EffectPlugin::paramsFlush(const clap_input_events* in, const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
}

// ---- state ----

bool EffectPlugin::stateSave(const clap_ostream* stream) noexcept {
    if (!streamWrite(stream, kStateMagic) || !streamWrite(stream, kStateVersion))
        return false;
    if (!streamWrite(stream, kParamCount))
        return false;
    for (clap_id id : {kParamGain, kParamLatency, kParamTail}) {
        const double value = paramStorage(id)->load(std::memory_order_relaxed);
        if (!streamWrite(stream, static_cast<uint32_t>(id)) || !streamWrite(stream, value))
            return false;
    }
    return true;
}

bool EffectPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0, count = 0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, count))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        double value = 0.0;
        if (!streamRead(stream, id) || !streamRead(stream, value))
            return false;
        if (auto* storage = paramStorage(id))
            storage->store(value, std::memory_order_relaxed);
        // Unknown ids are skipped: forward compatibility with newer states.
    }
    return true;
}

// ---- latency / tail / render ----

uint32_t EffectPlugin::latency() noexcept {
    return static_cast<uint32_t>(_latencySamples.load(std::memory_order_relaxed));
}

uint32_t EffectPlugin::tail() noexcept {
    return static_cast<uint32_t>(_tailSeconds.load(std::memory_order_relaxed) * sampleRate());
}

bool EffectPlugin::renderHasHardRealtimeRequirement() noexcept {
    return false;
}

bool EffectPlugin::renderSet(clap_plugin_render_mode mode) noexcept {
    if (mode != CLAP_RENDER_REALTIME && mode != CLAP_RENDER_OFFLINE)
        return false;
    _renderMode = mode;
    return true;
}

// ---- remote-controls ----

uint32_t EffectPlugin::remoteControlsPageCount() noexcept {
    return 1;
}

bool EffectPlugin::remoteControlsPage(uint32_t pageIndex,
                                      clap_remote_controls_page* page) noexcept {
    if (pageIndex != 0)
        return false;
    const clap_id params[] = {kParamGain, kParamLatency, kParamTail};
    ext::fillRemoteControlsPage(page, 0, "Validator", "Main", params, 3);
    return true;
}

// ---- processing ----

clap_process_status EffectPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;

    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            applyParamEvent(process->in_events->get(process->in_events, i));
    }

    const double gain = std::pow(10.0, _gainDb.load(std::memory_order_relaxed) / 20.0);

    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels =
        in.channel_count < out.channel_count ? in.channel_count : out.channel_count;
    if ((!in.data32 && !in.data64) || (!out.data32 && !out.data64))
        return CLAP_PROCESS_ERROR; // reported as P09 by the base class

    // Both ports support 32- and 64-bit. The declared REQUIRES_COMMON_SAMPLE_
    // SIZE makes mixed formats illegal (reported as P10 by the base class),
    // but they are still processed with conversion so audio keeps flowing.
    // The gain pass doubles as the input-silence scan for the tail logic.
    bool inputSilent = true;
    const auto applyGain = [&](const auto* src, auto* dst) {
        for (uint32_t i = 0; i < frames; ++i) {
            if (src[i] != 0)
                inputSilent = false;
            dst[i] = static_cast<std::remove_reference_t<decltype(dst[0])>>(src[i] * gain);
        }
    };
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (out.data64) {
            if (in.data64)
                applyGain(in.data64[ch], out.data64[ch]);
            else
                applyGain(in.data32[ch], out.data64[ch]);
        } else {
            if (in.data64)
                applyGain(in.data64[ch], out.data32[ch]);
            else
                applyGain(in.data32[ch], out.data32[ch]);
        }
    }

    // clap.tail behavior expressed through the return code: while the input
    // is live the host must keep processing (CONTINUE); once it falls silent
    // the declared tail window applies (TAIL — the host consults
    // clap_plugin_tail.get() to know how long); after the tail has elapsed
    // the plugin may be suspended until events or input return (SLEEP).
    if (!inputSilent) {
        _silentInputSamples = 0;
        return CLAP_PROCESS_CONTINUE;
    }
    _silentInputSamples += frames;
    return _silentInputSamples <= tail() ? CLAP_PROCESS_TAIL : CLAP_PROCESS_SLEEP;
}

} // namespace cvp
