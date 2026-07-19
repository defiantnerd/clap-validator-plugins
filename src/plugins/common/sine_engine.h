#pragma once

#include <clap/clap.h>

#include <cstdint>

namespace cvp {

// Minimal polyphonic sine engine shared by the synth flavors.
// 8 voices, instant attack, short linear release, oldest-voice stealing.
// Emits CLAP_EVENT_NOTE_END to the host when a voice finishes.
class SineEngine {
public:
    static constexpr uint32_t kMaxVoices = 8;

    void setSampleRate(double sampleRate) noexcept;
    void reset() noexcept;

    // Handles CLAP note on/off/choke and MIDI 0x8x/0x9x events; ignores others.
    void handleEvent(const clap_event_header* header) noexcept;

    // Adds the active voices into left/right at [offset, offset+frames), scaled by gain.
    void render(float* left, float* right, uint32_t offset, uint32_t frames,
                float gain) noexcept;

    // Sends CLAP_EVENT_NOTE_END for voices that finished since the last call.
    void emitNoteEnds(const clap_output_events* out, uint32_t time) noexcept;

    bool anyVoiceActive() const noexcept;

private:
    struct Voice {
        bool active = false;
        bool releasing = false;
        bool justEnded = false;
        int16_t key = 0;
        int16_t channel = 0;
        int32_t noteId = -1;
        uint32_t age = 0;
        double phase = 0.0;
        double phaseInc = 0.0;
        float velocity = 0.0f;
        float releaseGain = 1.0f;
        float releaseStep = 0.0f;
    };

    void noteOn(int16_t key, int16_t channel, int32_t noteId, double velocity) noexcept;
    void noteOff(int16_t key, int16_t channel, int32_t noteId, bool choke) noexcept;

    Voice _voices[kMaxVoices];
    double _sampleRate = 48000.0;
    uint32_t _clock = 0;
};

} // namespace cvp
