#include "plugins/common/sine_engine.h"

#include <cmath>
#include <cstring>

namespace cvp {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kReleaseSeconds = 0.05;

double keyToFrequency(int16_t key) {
    return 440.0 * std::pow(2.0, (key - 69) / 12.0);
}

} // namespace

void SineEngine::setSampleRate(double sampleRate) noexcept {
    _sampleRate = sampleRate;
}

void SineEngine::reset() noexcept {
    for (auto& voice : _voices)
        voice = Voice{};
    _clock = 0;
}

void SineEngine::noteOn(int16_t key, int16_t channel, int32_t noteId, double velocity) noexcept {
    Voice* target = nullptr;
    for (auto& voice : _voices) {
        if (!voice.active) {
            target = &voice;
            break;
        }
    }
    if (!target) { // steal the oldest voice
        target = &_voices[0];
        for (auto& voice : _voices)
            if (voice.age < target->age)
                target = &voice;
    }
    *target = Voice{};
    target->active = true;
    target->key = key;
    target->channel = channel;
    target->noteId = noteId;
    target->age = _clock++;
    target->phaseInc = kTwoPi * keyToFrequency(key) / _sampleRate;
    target->velocity = static_cast<float>(velocity <= 0.0 ? 1.0 : velocity);
    target->releaseStep = static_cast<float>(1.0 / (kReleaseSeconds * _sampleRate));
}

void SineEngine::noteOff(int16_t key, int16_t channel, int32_t noteId, bool choke) noexcept {
    for (auto& voice : _voices) {
        if (!voice.active)
            continue;
        // -1 acts as a wildcard for each address component.
        if ((key >= 0 && voice.key != key) || (channel >= 0 && voice.channel != channel) ||
            (noteId >= 0 && voice.noteId != noteId))
            continue;
        if (choke) {
            voice.active = false;
            voice.justEnded = true;
        } else {
            voice.releasing = true;
        }
    }
}

void SineEngine::handleEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;
    switch (header->type) {
    case CLAP_EVENT_NOTE_ON: {
        const auto* event = reinterpret_cast<const clap_event_note*>(header);
        noteOn(event->key, event->channel, event->note_id, event->velocity);
        break;
    }
    case CLAP_EVENT_NOTE_OFF: {
        const auto* event = reinterpret_cast<const clap_event_note*>(header);
        noteOff(event->key, event->channel, event->note_id, false);
        break;
    }
    case CLAP_EVENT_NOTE_CHOKE: {
        const auto* event = reinterpret_cast<const clap_event_note*>(header);
        noteOff(event->key, event->channel, event->note_id, true);
        break;
    }
    case CLAP_EVENT_MIDI: {
        const auto* event = reinterpret_cast<const clap_event_midi*>(header);
        const uint8_t status = event->data[0] & 0xF0;
        const auto channel = static_cast<int16_t>(event->data[0] & 0x0F);
        const auto key = static_cast<int16_t>(event->data[1]);
        if (status == 0x90 && event->data[2] > 0)
            noteOn(key, channel, -1, event->data[2] / 127.0);
        else if (status == 0x80 || (status == 0x90 && event->data[2] == 0))
            noteOff(key, channel, -1, false);
        break;
    }
    default:
        break;
    }
}

void SineEngine::render(float* left, float* right, uint32_t offset, uint32_t frames,
                        float gain) noexcept {
    for (auto& voice : _voices) {
        if (!voice.active)
            continue;
        for (uint32_t i = offset; i < offset + frames; ++i) {
            float envelope = voice.velocity * gain;
            if (voice.releasing) {
                voice.releaseGain -= voice.releaseStep;
                if (voice.releaseGain <= 0.0f) {
                    voice.active = false;
                    voice.justEnded = true;
                    break;
                }
                envelope *= voice.releaseGain;
            }
            const auto sample = static_cast<float>(std::sin(voice.phase)) * envelope;
            voice.phase += voice.phaseInc;
            if (voice.phase >= kTwoPi)
                voice.phase -= kTwoPi;
            left[i] += sample;
            if (right)
                right[i] += sample;
        }
    }
}

void SineEngine::emitNoteEnds(const clap_output_events* out, uint32_t time) noexcept {
    for (auto& voice : _voices) {
        if (!voice.justEnded)
            continue;
        voice.justEnded = false;
        clap_event_note event{};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_END;
        event.header.flags = 0;
        event.port_index = 0;
        event.key = voice.key;
        event.channel = voice.channel;
        event.note_id = voice.noteId;
        event.velocity = 0.0;
        out->try_push(out, &event.header);
    }
}

bool SineEngine::anyVoiceActive() const noexcept {
    for (const auto& voice : _voices)
        if (voice.active)
            return true;
    return false;
}

} // namespace cvp
