#include "plugins/synth/synth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

} // namespace

const clap_plugin_descriptor SynthPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.synth",
    .name = "Validator Synth",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "8-voice polyphonic sine synthesizer with note-ports in, stereo audio out, "
                   "params, state and voice-info. The output port supports AND prefers 64-bit "
                   "processing. Deliberately absent: latency, tail.",
    .features = kFeatures,
};

const clap_plugin* SynthPlugin::create(const clap_host* host) {
    return (new SynthPlugin(host))->clapPlugin();
}

SynthPlugin::SynthPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_NOTE_PORTS, ext::notePortsVtable(),
                     static_cast<ext::NotePortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_VOICE_INFO, ext::voiceInfoVtable(),
                     static_cast<ext::VoiceInfoProvider*>(this));
    // Deliberately NOT: CLAP_EXT_LATENCY, CLAP_EXT_TAIL.
}

bool SynthPlugin::activate(double sampleRate, uint32_t, uint32_t) noexcept {
    _engine.setSampleRate(sampleRate);
    _engine.reset();
    return true;
}

void SynthPlugin::reset() noexcept {
    _engine.reset();
}

// ---- audio-ports ----

uint32_t SynthPlugin::audioPortCount(bool isInput) noexcept {
    return isInput ? 0 : 1;
}

bool SynthPlugin::audioPortInfo(uint32_t index, bool isInput,
                                clap_audio_port_info* info) noexcept {
    if (isInput || index != 0)
        return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "Output");
    // Supports AND prefers 64-bit: a host honoring the preference should hand
    // this port data64 buffers (the sidechain synth stays 32-bit-only as the
    // deliberate contrast).
    info->flags =
        CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS | CLAP_AUDIO_PORT_PREFERS_64BITS;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

// ---- note-ports ----

uint32_t SynthPlugin::notePortCount(bool isInput) noexcept {
    return isInput ? 1 : 0;
}

bool SynthPlugin::notePortInfo(uint32_t index, bool isInput,
                               clap_note_port_info* info) noexcept {
    if (!isInput || index != 0)
        return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "Note In");
    return true;
}

// ---- params ----

uint32_t SynthPlugin::paramCount() noexcept {
    return 1;
}

bool SynthPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kParamVolume;
    std::snprintf(info->name, sizeof(info->name), "Volume");
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.7;
    return true;
}

bool SynthPlugin::paramValue(clap_id paramId, double* value) noexcept {
    if (paramId != kParamVolume)
        return false;
    *value = _volume.load(std::memory_order_relaxed);
    return true;
}

bool SynthPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                   uint32_t capacity) noexcept {
    if (paramId != kParamVolume)
        return false;
    std::snprintf(out, capacity, "%.0f %%", value * 100.0);
    return true;
}

bool SynthPlugin::paramTextToValue(clap_id paramId, const char* text, double* value) noexcept {
    if (paramId != kParamVolume)
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed > 1.0 ? parsed / 100.0 : parsed;
    return true;
}

void SynthPlugin::handleEvent(const clap_event_header* header) noexcept {
    if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
        if (event->param_id == kParamVolume)
            _volume.store(event->value, std::memory_order_relaxed);
        return;
    }
    _engine.handleEvent(header);
}

void SynthPlugin::paramsFlush(const clap_input_events* in, const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const auto* header = in->get(in, i);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID &&
            header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
            if (event->param_id == kParamVolume)
                _volume.store(event->value, std::memory_order_relaxed);
        }
    }
}

// ---- state ----

bool SynthPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double volume = _volume.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, volume);
}

bool SynthPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double volume = 0.7;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, volume))
        return false;
    _volume.store(volume, std::memory_order_relaxed);
    return true;
}

// ---- voice-info ----

bool SynthPlugin::voiceInfo(clap_voice_info* info) noexcept {
    info->voice_count = SineEngine::kMaxVoices;
    info->voice_capacity = SineEngine::kMaxVoices;
    info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
    return true;
}

// ---- processing ----

clap_process_status SynthPlugin::process(const clap_process* process) noexcept {
    if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 1)
        return CLAP_PROCESS_ERROR;

    // The output port supports both sample sizes: render into whichever the
    // host provided (a missing buffer is reported as P09 by the base class).
    auto& out = process->audio_outputs[0];
    if (out.data64)
        return processTyped(process, out.data64[0],
                            out.channel_count > 1 ? out.data64[1] : nullptr);
    if (out.data32)
        return processTyped(process, out.data32[0],
                            out.channel_count > 1 ? out.data32[1] : nullptr);
    return CLAP_PROCESS_ERROR;
}

template <typename Sample>
clap_process_status SynthPlugin::processTyped(const clap_process* process, Sample* left,
                                              Sample* right) noexcept {
    const uint32_t frames = process->frames_count;
    std::memset(left, 0, frames * sizeof(Sample));
    if (right)
        std::memset(right, 0, frames * sizeof(Sample));

    const auto gain = static_cast<float>(_volume.load(std::memory_order_relaxed));

    // Render in chunks between events so notes start/stop sample-accurately.
    const uint32_t eventCount = process->in_events->size(process->in_events);
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < eventCount; ++i) {
        const auto* header = process->in_events->get(process->in_events, i);
        if (header->time > cursor && header->time <= frames) {
            _engine.render(left, right, cursor, header->time - cursor, gain);
            cursor = header->time;
        }
        handleEvent(header);
    }
    if (cursor < frames)
        _engine.render(left, right, cursor, frames - cursor, gain);

    _engine.emitNoteEnds(process->out_events, frames > 0 ? frames - 1 : 0);

    return _engine.anyVoiceActive() ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

} // namespace cvp
