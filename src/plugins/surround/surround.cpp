#include "plugins/surround/surround.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_SURROUND, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

constexpr uint32_t kMaxChannels = 8;

struct Layout {
    const char* name;
    uint32_t channelCount;
    uint8_t channelMap[kMaxChannels];
};

const Layout kLayouts[] = {
    {"Quad 4.0", 4, {CLAP_SURROUND_FL, CLAP_SURROUND_FR, CLAP_SURROUND_BL, CLAP_SURROUND_BR}},
    {"5.1", 6,
     {CLAP_SURROUND_FL, CLAP_SURROUND_FR, CLAP_SURROUND_FC, CLAP_SURROUND_LFE, CLAP_SURROUND_BL,
      CLAP_SURROUND_BR}},
    {"7.1", 8,
     {CLAP_SURROUND_FL, CLAP_SURROUND_FR, CLAP_SURROUND_FC, CLAP_SURROUND_LFE, CLAP_SURROUND_BL,
      CLAP_SURROUND_BR, CLAP_SURROUND_SL, CLAP_SURROUND_SR}},
};
constexpr uint32_t kLayoutCount = sizeof(kLayouts) / sizeof(kLayouts[0]);

uint64_t layoutMask(const Layout& layout) {
    uint64_t mask = 0;
    for (uint32_t i = 0; i < layout.channelCount; ++i)
        mask |= uint64_t{1} << layout.channelMap[i];
    return mask;
}

// Solo values 1..8 select a surround channel identifier, not an index.
const struct {
    const char* name;
    uint8_t identifier;
} kSoloChoices[] = {
    {"Off", 0xFF},
    {"FL", CLAP_SURROUND_FL},
    {"FR", CLAP_SURROUND_FR},
    {"FC", CLAP_SURROUND_FC},
    {"LFE", CLAP_SURROUND_LFE},
    {"BL", CLAP_SURROUND_BL},
    {"BR", CLAP_SURROUND_BR},
    {"SL", CLAP_SURROUND_SL},
    {"SR", CLAP_SURROUND_SR},
};
constexpr int kSoloChoiceCount = sizeof(kSoloChoices) / sizeof(kSoloChoices[0]);

int soloIndexFromValue(double value) {
    const int index = static_cast<int>(value + 0.5);
    return index < 0 ? 0 : (index >= kSoloChoiceCount ? kSoloChoiceCount - 1 : index);
}

} // namespace

const clap_plugin_descriptor SurroundPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.surround",
    .name = "Validator Surround",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Surround passthrough with host-switchable layouts (Quad 4.0, 5.1, 7.1) via "
                   "configurable-audio-ports — mono/stereo are rejected. The Solo Channel "
                   "parameter makes wrong host channel mapping audible.",
    .features = kFeatures,
};

const clap_plugin* SurroundPlugin::create(const clap_host* host) {
    return (new SurroundPlugin(host))->clapPlugin();
}

SurroundPlugin::SurroundPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_SURROUND, ext::surroundVtable(),
                     static_cast<ext::SurroundProvider*>(this));
    provideExtension(CLAP_EXT_SURROUND_COMPAT, ext::surroundVtable(),
                     static_cast<ext::SurroundProvider*>(this));
    provideExtension(CLAP_EXT_CONFIGURABLE_AUDIO_PORTS, ext::configurableAudioPortsVtable(),
                     static_cast<ext::ConfigurableAudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT,
                     ext::configurableAudioPortsVtable(),
                     static_cast<ext::ConfigurableAudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS_COMPAT, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    // Deliberately NOT: CLAP_EXT_NOTE_PORTS, CLAP_EXT_AUDIO_PORTS_CONFIG.
}

// ---- audio-ports ----

uint32_t SurroundPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool SurroundPlugin::audioPortInfo(uint32_t index, bool isInput,
                                   clap_audio_port_info* info) noexcept {
    if (index != 0)
        return false;
    const auto& layout = kLayouts[_layoutIndex.load(std::memory_order_relaxed)];
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s (%s)", isInput ? "Input" : "Output",
                  layout.name);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = layout.channelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = 0;
    return true;
}

// ---- surround ----

bool SurroundPlugin::surroundIsChannelMaskSupported(uint64_t channelMask) noexcept {
    bool supported = false;
    for (const auto& layout : kLayouts)
        supported = supported || channelMask == layoutMask(layout);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "surround: is_channel_mask_supported(0x%llx) -> %s",
                  static_cast<unsigned long long>(channelMask), supported ? "true" : "false");
    logToHost(CLAP_LOG_DEBUG, buf);
    return supported;
}

uint32_t SurroundPlugin::surroundChannelMap(bool, uint32_t portIndex, uint8_t* channelMap,
                                            uint32_t capacity) noexcept {
    if (portIndex != 0)
        return 0;
    const auto& layout = kLayouts[_layoutIndex.load(std::memory_order_relaxed)];
    if (capacity < layout.channelCount) {
        logToHost(CLAP_LOG_HOST_MISBEHAVING,
                  "surround: get_channel_map() called with insufficient capacity");
        return 0;
    }
    std::memcpy(channelMap, layout.channelMap, layout.channelCount);
    return layout.channelCount;
}

// ---- configurable-audio-ports ----

int SurroundPlugin::layoutForRequests(const clap_audio_port_configuration_request* requests,
                                      uint32_t requestCount) noexcept {
    if (!requests || requestCount == 0)
        return -1;

    int layoutIndex = -1;
    for (uint32_t i = 0; i < requestCount; ++i) {
        const auto& request = requests[i];
        if (request.port_index != 0)
            return -1;
        if (request.port_type &&
            (std::strcmp(request.port_type, CLAP_PORT_MONO) == 0 ||
             std::strcmp(request.port_type, CLAP_PORT_STEREO) == 0))
            return -1; // surround only — mono/stereo explicitly rejected
        if (request.port_type && std::strcmp(request.port_type, CLAP_PORT_SURROUND) != 0)
            return -1;

        int match = -1;
        for (uint32_t l = 0; l < kLayoutCount; ++l)
            if (kLayouts[l].channelCount == request.channel_count)
                match = static_cast<int>(l);
        if (match < 0)
            return -1;

        // A supplied channel map must be exactly ours for that layout.
        if (request.port_details) {
            const auto* map = static_cast<const uint8_t*>(request.port_details);
            if (std::memcmp(map, kLayouts[match].channelMap, request.channel_count) != 0)
                return -1;
        }

        // All requests must agree on one layout (in/out stay symmetric).
        if (layoutIndex >= 0 && layoutIndex != match)
            return -1;
        layoutIndex = match;
    }
    return layoutIndex;
}

bool SurroundPlugin::canApplyConfiguration(const clap_audio_port_configuration_request* requests,
                                           uint32_t requestCount) noexcept {
    if (isActive()) {
        logToHost(CLAP_LOG_HOST_MISBEHAVING,
                  "configurable-audio-ports: can_apply_configuration() called while active");
        return false;
    }
    const int layout = layoutForRequests(requests, requestCount);
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "configurable-audio-ports: can_apply(%u request(s)) -> %s", requestCount,
                  layout >= 0 ? kLayouts[layout].name : "rejected");
    logToHost(CLAP_LOG_DEBUG, buf);
    return layout >= 0;
}

bool SurroundPlugin::applyConfiguration(const clap_audio_port_configuration_request* requests,
                                        uint32_t requestCount) noexcept {
    if (isActive()) {
        logToHost(CLAP_LOG_HOST_MISBEHAVING,
                  "configurable-audio-ports: apply_configuration() called while active");
        return false;
    }
    const int layout = layoutForRequests(requests, requestCount);
    if (layout < 0) {
        logToHost(CLAP_LOG_WARNING, "configurable-audio-ports: apply_configuration() rejected");
        return false;
    }
    _layoutIndex.store(static_cast<uint32_t>(layout), std::memory_order_relaxed);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "configurable-audio-ports: applied layout '%s' (%u channels)",
                  kLayouts[layout].name, kLayouts[layout].channelCount);
    logToHost(CLAP_LOG_INFO, buf);
    return true;
}

// ---- params ----

std::atomic<double>* SurroundPlugin::paramStorage(clap_id paramId) noexcept {
    switch (paramId) {
    case kParamGain:
        return &_gainDb;
    case kParamSolo:
        return &_solo;
    default:
        return nullptr;
    }
}

uint32_t SurroundPlugin::paramCount() noexcept {
    return 2;
}

bool SurroundPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index >= 2)
        return false;
    std::memset(info, 0, sizeof(*info));
    if (index == 0) {
        info->id = kParamGain;
        std::snprintf(info->name, sizeof(info->name), "Gain");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = -36.0;
        info->max_value = 36.0;
        info->default_value = 0.0;
    } else {
        info->id = kParamSolo;
        std::snprintf(info->name, sizeof(info->name), "Solo Channel");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        info->min_value = 0.0;
        info->max_value = kSoloChoiceCount - 1;
        info->default_value = 0.0;
    }
    return true;
}

bool SurroundPlugin::paramValue(clap_id paramId, double* value) noexcept {
    const auto* storage = paramStorage(paramId);
    if (!storage)
        return false;
    *value = storage->load(std::memory_order_relaxed);
    return true;
}

bool SurroundPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                      uint32_t capacity) noexcept {
    switch (paramId) {
    case kParamGain:
        std::snprintf(out, capacity, "%.1f dB", value);
        return true;
    case kParamSolo:
        std::snprintf(out, capacity, "%s", kSoloChoices[soloIndexFromValue(value)].name);
        return true;
    default:
        return false;
    }
}

bool SurroundPlugin::paramTextToValue(clap_id paramId, const char* text,
                                      double* value) noexcept {
    switch (paramId) {
    case kParamGain: {
        char* end = nullptr;
        const double parsed = std::strtod(text, &end);
        if (end == text)
            return false;
        *value = parsed;
        return true;
    }
    case kParamSolo:
        for (int i = 0; i < kSoloChoiceCount; ++i) {
            if (std::strcmp(text, kSoloChoices[i].name) == 0) {
                *value = i;
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

void SurroundPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    if (auto* storage = paramStorage(event->param_id))
        storage->store(event->value, std::memory_order_relaxed);
}

void SurroundPlugin::paramsFlush(const clap_input_events* in,
                                 const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
}

// ---- state ----

bool SurroundPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double gain = _gainDb.load(std::memory_order_relaxed);
    const double solo = _solo.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, gain) && streamWrite(stream, solo);
}

bool SurroundPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double gain = 0.0, solo = 0.0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, gain) || !streamRead(stream, solo))
        return false;
    _gainDb.store(gain, std::memory_order_relaxed);
    _solo.store(solo, std::memory_order_relaxed);
    return true;
}

// ---- remote-controls ----

uint32_t SurroundPlugin::remoteControlsPageCount() noexcept {
    return 1;
}

bool SurroundPlugin::remoteControlsPage(uint32_t pageIndex,
                                        clap_remote_controls_page* page) noexcept {
    if (pageIndex != 0)
        return false;
    const clap_id params[] = {kParamGain, kParamSolo};
    ext::fillRemoteControlsPage(page, 0, "Validator", "Main", params, 2);
    return true;
}

// ---- processing ----

clap_process_status SurroundPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;

    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            applyParamEvent(process->in_events->get(process->in_events, i));
    }

    const auto& layout = kLayouts[_layoutIndex.load(std::memory_order_relaxed)];
    const float gain =
        static_cast<float>(std::pow(10.0, _gainDb.load(std::memory_order_relaxed) / 20.0));
    const uint8_t soloId =
        kSoloChoices[soloIndexFromValue(_solo.load(std::memory_order_relaxed))].identifier;

    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    uint32_t channels = in.channel_count < out.channel_count ? in.channel_count
                                                             : out.channel_count;
    if (channels > layout.channelCount)
        channels = layout.channelCount;

    for (uint32_t ch = 0; ch < channels; ++ch) {
        // Solo selects a channel identifier, so it means the same speaker in
        // every layout.
        const bool audible = soloId == 0xFF || layout.channelMap[ch] == soloId;
        const float channelGain = audible ? gain : 0.0f;
        const float* src = in.data32[ch];
        float* dst = out.data32[ch];
        for (uint32_t i = 0; i < frames; ++i)
            dst[i] = src[i] * channelGain;
    }
    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
