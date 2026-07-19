#include "plugins/notefx/notefx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

int clampKey(int key) {
    return key < 0 ? 0 : (key > 127 ? 127 : key);
}

} // namespace

const clap_plugin_descriptor NoteFxPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.notefx",
    .name = "Validator NoteFX",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugin",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugin/issues",
    .version = "0.1.0",
    .description = "Pure note transformer (transpose) with note-ports, params and state — the "
                   "clap.audio-ports extension is deliberately not implemented at all.",
    .features = kFeatures,
};

const clap_plugin* NoteFxPlugin::create(const clap_host* host) {
    return (new NoteFxPlugin(host))->clapPlugin();
}

NoteFxPlugin::NoteFxPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_NOTE_PORTS, ext::notePortsVtable(),
                     static_cast<ext::NotePortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    // Deliberately NOT: CLAP_EXT_AUDIO_PORTS — not even with zero ports.
}

// ---- note-ports ----

uint32_t NoteFxPlugin::notePortCount(bool) noexcept {
    return 1;
}

bool NoteFxPlugin::notePortInfo(uint32_t index, bool isInput,
                                clap_note_port_info* info) noexcept {
    if (index != 0)
        return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Note In" : "Note Out");
    return true;
}

// ---- params ----

uint32_t NoteFxPlugin::paramCount() noexcept {
    return 1;
}

bool NoteFxPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kParamTranspose;
    std::snprintf(info->name, sizeof(info->name), "Transpose");
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
    info->min_value = -24.0;
    info->max_value = 24.0;
    info->default_value = 0.0;
    return true;
}

bool NoteFxPlugin::paramValue(clap_id paramId, double* value) noexcept {
    if (paramId != kParamTranspose)
        return false;
    *value = _transpose.load(std::memory_order_relaxed);
    return true;
}

bool NoteFxPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                    uint32_t capacity) noexcept {
    if (paramId != kParamTranspose)
        return false;
    std::snprintf(out, capacity, "%+d st", static_cast<int>(value));
    return true;
}

bool NoteFxPlugin::paramTextToValue(clap_id paramId, const char* text, double* value) noexcept {
    if (paramId != kParamTranspose)
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed;
    return true;
}

void NoteFxPlugin::paramsFlush(const clap_input_events* in,
                               const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const auto* header = in->get(in, i);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID &&
            header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
            if (event->param_id == kParamTranspose)
                _transpose.store(event->value, std::memory_order_relaxed);
        }
    }
}

// ---- state ----

bool NoteFxPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double transpose = _transpose.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, transpose);
}

bool NoteFxPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double transpose = 0.0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, transpose))
        return false;
    _transpose.store(transpose, std::memory_order_relaxed);
    return true;
}

// ---- processing ----

void NoteFxPlugin::transformEvent(const clap_event_header* header,
                                  const clap_output_events* out) noexcept {
    const int transpose = static_cast<int>(_transpose.load(std::memory_order_relaxed));

    if (header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
        switch (header->type) {
        case CLAP_EVENT_PARAM_VALUE: {
            const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
            if (event->param_id == kParamTranspose)
                _transpose.store(event->value, std::memory_order_relaxed);
            return; // consumed, not forwarded
        }
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE: {
            clap_event_note event = *reinterpret_cast<const clap_event_note*>(header);
            if (event.key >= 0) // -1 is the wildcard and must stay -1
                event.key = static_cast<int16_t>(clampKey(event.key + transpose));
            out->try_push(out, &event.header);
            return;
        }
        case CLAP_EVENT_MIDI: {
            clap_event_midi event = *reinterpret_cast<const clap_event_midi*>(header);
            const uint8_t status = event.data[0] & 0xF0;
            if (status == 0x80 || status == 0x90)
                event.data[1] = static_cast<uint8_t>(clampKey(event.data[1] + transpose));
            out->try_push(out, &event.header);
            return;
        }
        default:
            break;
        }
    }
    // Anything else passes through untouched.
    out->try_push(out, header);
}

clap_process_status NoteFxPlugin::process(const clap_process* process) noexcept {
    const uint32_t count = process->in_events->size(process->in_events);
    for (uint32_t i = 0; i < count; ++i)
        transformEvent(process->in_events->get(process->in_events, i), process->out_events);
    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
