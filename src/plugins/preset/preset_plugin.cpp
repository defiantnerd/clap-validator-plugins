#include "plugins/preset/preset_plugin.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "plugins/preset/preset_shared.h"
#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

const char* kColorNames[] = {"Neutral", "Warm", "Bright", "Dark"};

} // namespace

const clap_plugin_descriptor PresetPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = preset::kPresetPluginId,
    .name = "Validator Preset",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Gain effect loadable from presets in both location kinds: internal factory "
                   "presets (PLUGIN kind, via load_key) and .cvpreset files (FILE kind), with a "
                   "matching preset-discovery provider.",
    .features = kFeatures,
};

const clap_plugin* PresetPlugin::create(const clap_host* host) {
    return (new PresetPlugin(host))->clapPlugin();
}

PresetPlugin::PresetPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_PRESET_LOAD, ext::presetLoadVtable(),
                     static_cast<ext::PresetLoadProvider*>(this));
    provideExtension(CLAP_EXT_PRESET_LOAD_COMPAT, ext::presetLoadVtable(),
                     static_cast<ext::PresetLoadProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS_COMPAT, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    // Deliberately NOT: CLAP_EXT_NOTE_PORTS.
}

bool PresetPlugin::init() noexcept {
    if (host()) {
        _hostPresetLoad = static_cast<const clap_host_preset_load*>(
            host()->get_extension(host(), CLAP_EXT_PRESET_LOAD));
        if (!_hostPresetLoad)
            _hostPresetLoad = static_cast<const clap_host_preset_load*>(
                host()->get_extension(host(), CLAP_EXT_PRESET_LOAD_COMPAT));
        _hostParams =
            static_cast<const clap_host_params*>(host()->get_extension(host(), CLAP_EXT_PARAMS));
    }
    return true;
}

// ---- audio-ports ----

uint32_t PresetPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool PresetPlugin::audioPortInfo(uint32_t index, bool isInput,
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

std::atomic<double>* PresetPlugin::paramStorage(clap_id paramId) noexcept {
    switch (paramId) {
    case kParamGain:
        return &_gainDb;
    case kParamColor:
        return &_color;
    default:
        return nullptr;
    }
}

uint32_t PresetPlugin::paramCount() noexcept {
    return 2;
}

bool PresetPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
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
        info->id = kParamColor;
        std::snprintf(info->name, sizeof(info->name), "Color");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        info->min_value = 0.0;
        info->max_value = 3.0;
        info->default_value = 0.0;
    }
    return true;
}

bool PresetPlugin::paramValue(clap_id paramId, double* value) noexcept {
    const auto* storage = paramStorage(paramId);
    if (!storage)
        return false;
    *value = storage->load(std::memory_order_relaxed);
    return true;
}

bool PresetPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                    uint32_t capacity) noexcept {
    switch (paramId) {
    case kParamGain:
        std::snprintf(out, capacity, "%.1f dB", value);
        return true;
    case kParamColor: {
        const int index = value < 0.0 ? 0 : (value > 3.0 ? 3 : static_cast<int>(value + 0.5));
        std::snprintf(out, capacity, "%s", kColorNames[index]);
        return true;
    }
    default:
        return false;
    }
}

bool PresetPlugin::paramTextToValue(clap_id paramId, const char* text, double* value) noexcept {
    switch (paramId) {
    case kParamGain: {
        char* end = nullptr;
        const double parsed = std::strtod(text, &end);
        if (end == text)
            return false;
        *value = parsed;
        return true;
    }
    case kParamColor:
        for (int i = 0; i < 4; ++i) {
            if (std::strncmp(text, kColorNames[i], std::strlen(kColorNames[i])) == 0) {
                *value = i;
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

void PresetPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    if (auto* storage = paramStorage(event->param_id))
        storage->store(event->value, std::memory_order_relaxed);
}

void PresetPlugin::paramsFlush(const clap_input_events* in, const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
}

// ---- state ----

bool PresetPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double gain = _gainDb.load(std::memory_order_relaxed);
    const double color = _color.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, gain) && streamWrite(stream, color);
}

bool PresetPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double gain = 0.0, color = 0.0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, gain) || !streamRead(stream, color))
        return false;
    _gainDb.store(gain, std::memory_order_relaxed);
    _color.store(color, std::memory_order_relaxed);
    return true;
}

// ---- preset-load: both location kinds ----

bool PresetPlugin::presetLoadFromLocation(uint32_t locationKind, const char* location,
                                          const char* loadKey) noexcept {
    char buf[320];
    std::snprintf(buf, sizeof(buf), "preset-load: from_location(kind=%u, location=%s, key=%s)",
                  locationKind, location ? location : "null", loadKey ? loadKey : "null");
    logToHost(CLAP_LOG_INFO, buf);

    preset::PresetData data{};
    bool ok = false;

    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
        if (const auto* internal = preset::findInternalPreset(loadKey)) {
            std::snprintf(data.name, sizeof(data.name), "%s", internal->name);
            data.gain = internal->gain;
            data.color = internal->color;
            ok = true;
        }
    } else if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location) {
        ok = preset::parsePresetFile(location, &data);
    }

    if (!ok) {
        std::snprintf(buf, sizeof(buf), "preset-load: FAILED (kind=%u, location=%s, key=%s)",
                      locationKind, location ? location : "null", loadKey ? loadKey : "null");
        logToHost(CLAP_LOG_WARNING, buf);
        if (_hostPresetLoad && _hostPresetLoad->on_error && host())
            _hostPresetLoad->on_error(host(), locationKind, location, loadKey, 0,
                                      "unknown preset or unreadable preset file");
        return false;
    }

    _gainDb.store(data.gain, std::memory_order_relaxed);
    _color.store(data.color, std::memory_order_relaxed);

    // Values changed outside the event stream: tell the host to re-read them.
    if (_hostParams && _hostParams->rescan && host())
        _hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
    if (_hostPresetLoad && _hostPresetLoad->loaded && host())
        _hostPresetLoad->loaded(host(), locationKind, location, loadKey);

    std::snprintf(buf, sizeof(buf), "preset-load: loaded '%s' (gain=%.1f dB, color=%d)", data.name,
                  data.gain, static_cast<int>(data.color));
    logToHost(CLAP_LOG_INFO, buf);
    return true;
}

// ---- remote-controls ----

uint32_t PresetPlugin::remoteControlsPageCount() noexcept {
    return 1;
}

bool PresetPlugin::remoteControlsPage(uint32_t pageIndex,
                                      clap_remote_controls_page* page) noexcept {
    if (pageIndex != 0)
        return false;
    const clap_id params[] = {kParamGain, kParamColor};
    ext::fillRemoteControlsPage(page, 0, "Validator", "Main", params, 2);
    return true;
}

// ---- processing ----

clap_process_status PresetPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;

    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            applyParamEvent(process->in_events->get(process->in_events, i));
    }

    const float gain =
        static_cast<float>(std::pow(10.0, _gainDb.load(std::memory_order_relaxed) / 20.0));
    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels =
        in.channel_count < out.channel_count ? in.channel_count : out.channel_count;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        const float* src = in.data32[ch];
        float* dst = out.data32[ch];
        for (uint32_t i = 0; i < frames; ++i)
            dst[i] = src[i] * gain;
    }
    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
