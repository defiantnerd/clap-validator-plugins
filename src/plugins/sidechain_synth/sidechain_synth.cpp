#include "plugins/sidechain_synth/sidechain_synth.h"

#include <cmath>
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
constexpr float kFollowerCoeff = 0.01f;

} // namespace

const clap_plugin_descriptor SidechainSynthPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.sidechain-synth",
    .name = "Validator Sidechain Synth",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description = "Sine synthesizer whose output is gated by a non-main stereo sidechain "
                   "input — tests hosts' audio-into-instrument routing. Note names label the "
                   "C major triad as suggested gate-test material.",
    .features = kFeatures,
};

const clap_plugin* SidechainSynthPlugin::create(const clap_host* host) {
    return (new SidechainSynthPlugin(host))->clapPlugin();
}

SidechainSynthPlugin::SidechainSynthPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_NOTE_NAME, ext::noteNameVtable(),
                     static_cast<ext::NoteNameProvider*>(this));
    provideExtension(CLAP_EXT_NOTE_PORTS, ext::notePortsVtable(),
                     static_cast<ext::NotePortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS_COMPAT, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    // Deliberately NOT: CLAP_EXT_LATENCY, CLAP_EXT_TAIL, CLAP_EXT_VOICE_INFO.
}

bool SidechainSynthPlugin::activate(double sampleRate, uint32_t, uint32_t) noexcept {
    _engine.setSampleRate(sampleRate);
    _engine.reset();
    _envelope = 0.0f;
    return true;
}

void SidechainSynthPlugin::reset() noexcept {
    _engine.reset();
    _envelope = 0.0f;
}

// ---- audio-ports ----

uint32_t SidechainSynthPlugin::audioPortCount(bool) noexcept {
    return 1; // one input (sidechain, non-main), one output (main)
}

bool SidechainSynthPlugin::audioPortInfo(uint32_t index, bool isInput,
                                         clap_audio_port_info* info) noexcept {
    if (index != 0)
        return false;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    if (isInput) {
        info->id = 1;
        std::snprintf(info->name, sizeof(info->name), "Sidechain");
        info->flags = 0; // deliberately NOT CLAP_AUDIO_PORT_IS_MAIN
    } else {
        info->id = 0;
        std::snprintf(info->name, sizeof(info->name), "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    }
    return true;
}

// ---- note-ports ----

uint32_t SidechainSynthPlugin::notePortCount(bool isInput) noexcept {
    return isInput ? 1 : 0;
}

bool SidechainSynthPlugin::notePortInfo(uint32_t index, bool isInput,
                                        clap_note_port_info* info) noexcept {
    if (!isInput || index != 0)
        return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "Note In");
    return true;
}

// ---- note-name ----
// A deliberately different set from the Validator Synth: hosts must query
// names per plugin, not cache them per bundle. The C major triad around
// middle C is the suggested test material for the sidechain gating.

uint32_t SidechainSynthPlugin::noteNameCount() noexcept {
    return 3;
}

bool SidechainSynthPlugin::noteNameGet(uint32_t index, clap_note_name* noteName) noexcept {
    static constexpr struct {
        int16_t key;
        const char* name;
    } kNames[] = {
        {60, "Gate Test C4"},
        {64, "Gate Test E4"},
        {67, "Gate Test G4"},
    };
    if (index >= 3)
        return false;
    std::memset(noteName, 0, sizeof(*noteName));
    noteName->key = kNames[index].key;
    noteName->port = 0;
    noteName->channel = -1;
    std::snprintf(noteName->name, sizeof(noteName->name), "%s", kNames[index].name);
    return true;
}

// ---- params ----

std::atomic<double>* SidechainSynthPlugin::paramStorage(clap_id paramId) noexcept {
    switch (paramId) {
    case kParamVolume:
        return &_volume;
    case kParamSidechainAmount:
        return &_sidechainAmount;
    default:
        return nullptr;
    }
}

uint32_t SidechainSynthPlugin::paramCount() noexcept {
    return 2;
}

bool SidechainSynthPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index >= 2)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    if (index == 0) {
        info->id = kParamVolume;
        std::snprintf(info->name, sizeof(info->name), "Volume");
        info->default_value = 0.7;
    } else {
        info->id = kParamSidechainAmount;
        std::snprintf(info->name, sizeof(info->name), "Sidechain Amount");
        info->default_value = 1.0;
    }
    return true;
}

bool SidechainSynthPlugin::paramValue(clap_id paramId, double* value) noexcept {
    const auto* storage = paramStorage(paramId);
    if (!storage)
        return false;
    *value = storage->load(std::memory_order_relaxed);
    return true;
}

bool SidechainSynthPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                            uint32_t capacity) noexcept {
    if (!paramStorage(paramId))
        return false;
    std::snprintf(out, capacity, "%.0f %%", value * 100.0);
    return true;
}

bool SidechainSynthPlugin::paramTextToValue(clap_id paramId, const char* text,
                                            double* value) noexcept {
    if (!paramStorage(paramId))
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed > 1.0 ? parsed / 100.0 : parsed;
    return true;
}

void SidechainSynthPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    if (auto* storage = paramStorage(event->param_id))
        storage->store(event->value, std::memory_order_relaxed);
}

void SidechainSynthPlugin::handleEvent(const clap_event_header* header) noexcept {
    if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE) {
        applyParamEvent(header);
        return;
    }
    _engine.handleEvent(header);
}

void SidechainSynthPlugin::paramsFlush(const clap_input_events* in,
                                       const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
}

// ---- state ----

bool SidechainSynthPlugin::stateSave(const clap_ostream* stream) noexcept {
    const double volume = _volume.load(std::memory_order_relaxed);
    const double amount = _sidechainAmount.load(std::memory_order_relaxed);
    return streamWrite(stream, kStateMagic) && streamWrite(stream, kStateVersion) &&
           streamWrite(stream, volume) && streamWrite(stream, amount);
}

bool SidechainSynthPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0;
    double volume = 0.7, amount = 1.0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, volume) || !streamRead(stream, amount))
        return false;
    _volume.store(volume, std::memory_order_relaxed);
    _sidechainAmount.store(amount, std::memory_order_relaxed);
    return true;
}

// ---- remote-controls ----

uint32_t SidechainSynthPlugin::remoteControlsPageCount() noexcept {
    return 1;
}

bool SidechainSynthPlugin::remoteControlsPage(uint32_t pageIndex,
                                              clap_remote_controls_page* page) noexcept {
    if (pageIndex != 0)
        return false;
    const clap_id params[] = {kParamVolume, kParamSidechainAmount};
    ext::fillRemoteControlsPage(page, 0, "Validator", "Main", params, 2);
    return true;
}

// ---- processing ----

clap_process_status SidechainSynthPlugin::process(const clap_process* process) noexcept {
    if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 1)
        return CLAP_PROCESS_ERROR;

    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    float* left = out.data32[0];
    float* right = out.channel_count > 1 ? out.data32[1] : nullptr;
    std::memset(left, 0, frames * sizeof(float));
    if (right)
        std::memset(right, 0, frames * sizeof(float));

    const auto gain = static_cast<float>(_volume.load(std::memory_order_relaxed));

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

    // Modulate the rendered synth by the sidechain input's level, if connected.
    const auto amount = static_cast<float>(_sidechainAmount.load(std::memory_order_relaxed));
    const clap_audio_buffer* sidechain =
        process->audio_inputs_count > 0 && process->audio_inputs[0].channel_count > 0
            ? &process->audio_inputs[0]
            : nullptr;
    if (sidechain && amount > 0.0f) {
        const float* scLeft = sidechain->data32[0];
        const float* scRight =
            sidechain->channel_count > 1 ? sidechain->data32[1] : sidechain->data32[0];
        for (uint32_t i = 0; i < frames; ++i) {
            const float level = 0.5f * (std::fabs(scLeft[i]) + std::fabs(scRight[i]));
            _envelope += kFollowerCoeff * (level - _envelope);
            const float mod = (1.0f - amount) + amount * (_envelope > 1.0f ? 1.0f : _envelope);
            left[i] *= mod;
            if (right)
                right[i] *= mod;
        }
    }

    _engine.emitNoteEnds(process->out_events, frames > 0 ? frames - 1 : 0);

    return _engine.anyVoiceActive() ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

} // namespace cvp
