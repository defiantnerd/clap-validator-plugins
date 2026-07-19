#include "plugins/audioports_zero/audioports_zero.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

// "audio-effect" with zero audio ports is deliberate — the category is
// required by hosts/validators, the missing ports are the trap.
const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

} // namespace

const clap_plugin_descriptor AudioPortsZeroPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.audioports-zero",
    .name = "Validator AudioPortsZero",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Implements clap.audio-ports but reports zero ports in both directions — "
                   "hosts must handle a portless plugin.",
    .features = kFeatures,
};

const clap_plugin* AudioPortsZeroPlugin::create(const clap_host* host) {
    return (new AudioPortsZeroPlugin(host))->clapPlugin();
}

AudioPortsZeroPlugin::AudioPortsZeroPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
}

// ---- audio-ports: the whole point — extension present, zero ports ----

uint32_t AudioPortsZeroPlugin::audioPortCount(bool) noexcept {
    return 0;
}

bool AudioPortsZeroPlugin::audioPortInfo(uint32_t, bool, clap_audio_port_info*) noexcept {
    return false;
}

// ---- params ----

uint32_t AudioPortsZeroPlugin::paramCount() noexcept {
    return 1;
}

bool AudioPortsZeroPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kParamDummy;
    std::snprintf(info->name, sizeof(info->name), "Dummy");
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.5;
    return true;
}

bool AudioPortsZeroPlugin::paramValue(clap_id paramId, double* value) noexcept {
    if (paramId != kParamDummy)
        return false;
    *value = _dummy.load(std::memory_order_relaxed);
    return true;
}

bool AudioPortsZeroPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                            uint32_t capacity) noexcept {
    if (paramId != kParamDummy)
        return false;
    std::snprintf(out, capacity, "%.2f", value);
    return true;
}

bool AudioPortsZeroPlugin::paramTextToValue(clap_id paramId, const char* text,
                                            double* value) noexcept {
    if (paramId != kParamDummy)
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed;
    return true;
}

void AudioPortsZeroPlugin::paramsFlush(const clap_input_events* in,
                                       const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const auto* header = in->get(in, i);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID &&
            header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
            if (event->param_id == kParamDummy)
                _dummy.store(event->value, std::memory_order_relaxed);
        }
    }
}

// ---- state ----

bool AudioPortsZeroPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double dummy = _dummy.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, dummy);
}

bool AudioPortsZeroPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double dummy = 0.5;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, dummy))
        return false;
    _dummy.store(dummy, std::memory_order_relaxed);
    return true;
}

// ---- processing: nothing to do without ports ----

clap_process_status AudioPortsZeroPlugin::process(const clap_process* process) noexcept {
    paramsFlush(process->in_events, process->out_events);
    return CLAP_PROCESS_SLEEP;
}

} // namespace cvp
