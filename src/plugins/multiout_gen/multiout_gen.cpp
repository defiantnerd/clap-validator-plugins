#include "plugins/multiout_gen/multiout_gen.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;
constexpr double kTwoPi = 6.283185307179586;
// One distinct pitch per output port (A3, C#4, E4, G4, A4) — routing is
// verifiable by ear.
constexpr double kPortFrequencies[] = {220.0, 277.18, 329.63, 392.0, 440.0};

} // namespace

const clap_plugin_descriptor MultiOutGenPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.multiout-gen",
    .name = "Validator MultiOut Gen",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Test-tone generator with audio-ports-config: 'Stereo' (1 main out) or "
                   "'Multi Out' (main + 4 aux outs), a distinct sine pitch per port.",
    .features = kFeatures,
};

const clap_plugin* MultiOutGenPlugin::create(const clap_host* host) {
    return (new MultiOutGenPlugin(host))->clapPlugin();
}

MultiOutGenPlugin::MultiOutGenPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_AUDIO_PORTS_CONFIG, ext::audioPortsConfigVtable(),
                     static_cast<ext::AudioPortsConfigProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    // Deliberately NOT: CLAP_EXT_NOTE_PORTS.
}

bool MultiOutGenPlugin::activate(double, uint32_t, uint32_t) noexcept {
    return true;
}

void MultiOutGenPlugin::reset() noexcept {
    std::memset(_phases, 0, sizeof(_phases));
}

// ---- audio-ports ----

uint32_t MultiOutGenPlugin::currentPortCount() const noexcept {
    return _config == kConfigMultiOut ? kMaxPorts : 1;
}

uint32_t MultiOutGenPlugin::audioPortCount(bool isInput) noexcept {
    return isInput ? 0 : currentPortCount();
}

bool MultiOutGenPlugin::audioPortInfo(uint32_t index, bool isInput,
                                      clap_audio_port_info* info) noexcept {
    if (isInput || index >= currentPortCount())
        return false;
    // Main out keeps id 0 in both configs; aux ports are ids 1..4.
    info->id = index;
    if (index == 0) {
        std::snprintf(info->name, sizeof(info->name), "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    } else {
        std::snprintf(info->name, sizeof(info->name), "Aux %u", index);
        info->flags = 0;
    }
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

// ---- audio-ports-config ----

uint32_t MultiOutGenPlugin::audioPortsConfigCount() noexcept {
    return 2;
}

bool MultiOutGenPlugin::audioPortsConfigInfo(uint32_t index,
                                             clap_audio_ports_config* config) noexcept {
    if (index >= 2)
        return false;
    std::memset(config, 0, sizeof(*config));
    config->input_port_count = 0;
    config->has_main_input = false;
    // Meaningless without a main input, but some tools (clap-info) read it
    // unconditionally — keep it a valid string rather than nullptr.
    config->main_input_port_type = CLAP_PORT_STEREO;
    config->has_main_output = true;
    config->main_output_channel_count = 2;
    config->main_output_port_type = CLAP_PORT_STEREO;
    if (index == 0) {
        config->id = kConfigStereo;
        std::snprintf(config->name, sizeof(config->name), "Stereo");
        config->output_port_count = 1;
    } else {
        config->id = kConfigMultiOut;
        std::snprintf(config->name, sizeof(config->name), "Multi Out");
        config->output_port_count = kMaxPorts;
    }
    return true;
}

bool MultiOutGenPlugin::audioPortsConfigSelect(clap_id configId) noexcept {
    if (isActive())
        return false; // select() is only legal while deactivated
    if (configId != kConfigStereo && configId != kConfigMultiOut)
        return false;
    _config = configId;
    return true;
}

// ---- params ----

uint32_t MultiOutGenPlugin::paramCount() noexcept {
    return 1;
}

bool MultiOutGenPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kParamLevel;
    std::snprintf(info->name, sizeof(info->name), "Level");
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.5;
    return true;
}

bool MultiOutGenPlugin::paramValue(clap_id paramId, double* value) noexcept {
    if (paramId != kParamLevel)
        return false;
    *value = _level.load(std::memory_order_relaxed);
    return true;
}

bool MultiOutGenPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                         uint32_t capacity) noexcept {
    if (paramId != kParamLevel)
        return false;
    std::snprintf(out, capacity, "%.0f %%", value * 100.0);
    return true;
}

bool MultiOutGenPlugin::paramTextToValue(clap_id paramId, const char* text,
                                         double* value) noexcept {
    if (paramId != kParamLevel)
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed > 1.0 ? parsed / 100.0 : parsed;
    return true;
}

void MultiOutGenPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    if (event->param_id == kParamLevel)
        _level.store(event->value, std::memory_order_relaxed);
}

void MultiOutGenPlugin::paramsFlush(const clap_input_events* in,
                                    const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
}

// ---- state ----

bool MultiOutGenPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double level = _level.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, level);
}

bool MultiOutGenPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double level = 0.5;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, level))
        return false;
    _level.store(level, std::memory_order_relaxed);
    return true;
}

// ---- processing ----

clap_process_status MultiOutGenPlugin::process(const clap_process* process) noexcept {
    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            applyParamEvent(process->in_events->get(process->in_events, i));
    }

    const auto level = static_cast<float>(_level.load(std::memory_order_relaxed));
    const uint32_t frames = process->frames_count;
    const uint32_t ports =
        process->audio_outputs_count < kMaxPorts ? process->audio_outputs_count : kMaxPorts;

    for (uint32_t port = 0; port < ports; ++port) {
        auto& out = process->audio_outputs[port];
        const double phaseInc = kTwoPi * kPortFrequencies[port] / sampleRate();
        double phase = _phases[port];
        for (uint32_t ch = 0; ch < out.channel_count; ++ch) {
            phase = _phases[port];
            float* dst = out.data32[ch];
            for (uint32_t i = 0; i < frames; ++i) {
                dst[i] = static_cast<float>(std::sin(phase)) * level;
                phase += phaseInc;
                if (phase >= kTwoPi)
                    phase -= kTwoPi;
            }
        }
        _phases[port] = phase;
    }
    // With Level at zero the output is entirely quiet: hint that the host may
    // suspend processing (a param event raising Level must wake it again).
    // The voice-based synth flavors return CLAP_PROCESS_SLEEP instead, so the
    // suite exercises both host-side silence optimizations.
    return level > 0.0f ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_CONTINUE_IF_NOT_QUIET;
}

} // namespace cvp
