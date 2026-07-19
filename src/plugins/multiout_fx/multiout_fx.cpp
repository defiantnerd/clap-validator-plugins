#include "plugins/multiout_fx/multiout_fx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

} // namespace

const clap_plugin_descriptor MultiOutFxPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.multiout-fx",
    .name = "Validator MultiOut FX",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Stereo effect fanning its input to multiple outputs, with "
                   "audio-ports-config: 'Stereo' (1 in / 1 out) or 'Multi Out' (1 in / 3 out).",
    .features = kFeatures,
};

const clap_plugin* MultiOutFxPlugin::create(const clap_host* host) {
    return (new MultiOutFxPlugin(host))->clapPlugin();
}

MultiOutFxPlugin::MultiOutFxPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_AUDIO_PORTS_CONFIG, ext::audioPortsConfigVtable(),
                     static_cast<ext::AudioPortsConfigProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    // Deliberately NOT: CLAP_EXT_NOTE_PORTS.
}

// ---- audio-ports ----

uint32_t MultiOutFxPlugin::currentOutPortCount() const noexcept {
    return _config == kConfigMultiOut ? kMaxOutPorts : 1;
}

uint32_t MultiOutFxPlugin::audioPortCount(bool isInput) noexcept {
    return isInput ? 1 : currentOutPortCount();
}

bool MultiOutFxPlugin::audioPortInfo(uint32_t index, bool isInput,
                                     clap_audio_port_info* info) noexcept {
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    if (isInput) {
        if (index != 0)
            return false;
        info->id = 0;
        std::snprintf(info->name, sizeof(info->name), "Input");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->in_place_pair = 0; // may share buffers with the main output
        return true;
    }
    if (index >= currentOutPortCount())
        return false;
    // Main out keeps id 0 in both configs; aux ports are ids 1..2.
    info->id = index;
    info->in_place_pair = index == 0 ? 0 : CLAP_INVALID_ID;
    if (index == 0) {
        std::snprintf(info->name, sizeof(info->name), "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    } else {
        std::snprintf(info->name, sizeof(info->name), "Aux %u", index);
        info->flags = 0;
    }
    return true;
}

// ---- audio-ports-config ----

uint32_t MultiOutFxPlugin::audioPortsConfigCount() noexcept {
    return 2;
}

bool MultiOutFxPlugin::audioPortsConfigInfo(uint32_t index,
                                            clap_audio_ports_config* config) noexcept {
    if (index >= 2)
        return false;
    std::memset(config, 0, sizeof(*config));
    config->input_port_count = 1;
    config->has_main_input = true;
    config->main_input_channel_count = 2;
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
        config->output_port_count = kMaxOutPorts;
    }
    return true;
}

bool MultiOutFxPlugin::audioPortsConfigSelect(clap_id configId) noexcept {
    if (isActive())
        return false; // select() is only legal while deactivated
    if (configId != kConfigStereo && configId != kConfigMultiOut)
        return false;
    _config = configId;
    return true;
}

// ---- params ----

uint32_t MultiOutFxPlugin::paramCount() noexcept {
    return 1;
}

bool MultiOutFxPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kParamAuxLevel;
    std::snprintf(info->name, sizeof(info->name), "Aux Level");
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 1.0;
    return true;
}

bool MultiOutFxPlugin::paramValue(clap_id paramId, double* value) noexcept {
    if (paramId != kParamAuxLevel)
        return false;
    *value = _auxLevel.load(std::memory_order_relaxed);
    return true;
}

bool MultiOutFxPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                        uint32_t capacity) noexcept {
    if (paramId != kParamAuxLevel)
        return false;
    std::snprintf(out, capacity, "%.0f %%", value * 100.0);
    return true;
}

bool MultiOutFxPlugin::paramTextToValue(clap_id paramId, const char* text,
                                        double* value) noexcept {
    if (paramId != kParamAuxLevel)
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed > 1.0 ? parsed / 100.0 : parsed;
    return true;
}

void MultiOutFxPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    if (event->param_id == kParamAuxLevel)
        _auxLevel.store(event->value, std::memory_order_relaxed);
}

void MultiOutFxPlugin::paramsFlush(const clap_input_events* in,
                                   const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
}

// ---- state ----

bool MultiOutFxPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double auxLevel = _auxLevel.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, auxLevel);
}

bool MultiOutFxPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double auxLevel = 1.0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, auxLevel))
        return false;
    _auxLevel.store(auxLevel, std::memory_order_relaxed);
    return true;
}

// ---- processing ----

clap_process_status MultiOutFxPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;

    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            applyParamEvent(process->in_events->get(process->in_events, i));
    }

    const auto auxLevel = static_cast<float>(_auxLevel.load(std::memory_order_relaxed));
    const auto& in = process->audio_inputs[0];
    const uint32_t frames = process->frames_count;

    for (uint32_t port = 0; port < process->audio_outputs_count; ++port) {
        auto& out = process->audio_outputs[port];
        const float gain = port == 0 ? 1.0f : auxLevel;
        const uint32_t channels =
            in.channel_count < out.channel_count ? in.channel_count : out.channel_count;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const float* src = in.data32[ch];
            float* dst = out.data32[ch];
            for (uint32_t i = 0; i < frames; ++i)
                dst[i] = src[i] * gain;
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
