#pragma once

#include <clap/clap.h>

namespace cvp {

class LogBuffer;

// What a native view needs from its plugin. Implemented by the plugin,
// consumed by the platform views — keeps views free of plugin internals.
//
// Thread contract: the read methods (guiParamCount/Desc/Value/ValueText and
// guiLog) are [any-thread] — the X11 view calls them from its own UI thread.
// Implementations may only touch atomics and the mutex-guarded LogBuffer.
// The gesture/set methods are called from the view's UI thread and must
// queue work, never call host functions that aren't [thread-safe].
class GuiModel {
public:
    virtual ~GuiModel() = default;

    struct ParamDesc {
        clap_id id;
        char name[64];
        double minValue;
        double maxValue;
        double defaultValue;
        bool stepped;
    };

    // Snapshot of the last clap_event_transport seen in process() —
    // fixed-point beat/second values already converted to doubles.
    struct TransportInfo {
        bool seen = false;         // any transport received yet?
        bool provided = false;     // last process() had a non-null transport?
        uint32_t flags = 0;        // CLAP_TRANSPORT_* flags
        double tempo = 0.0;        // BPM
        double songPosBeats = 0.0;
        double songPosSeconds = 0.0;
        double loopStartBeats = 0.0;
        double loopEndBeats = 0.0;
        double barStartBeats = 0.0;
        int32_t barNumber = 0;
        uint16_t tsigNum = 0;
        uint16_t tsigDenom = 0;
    };

    virtual uint32_t guiParamCount() noexcept = 0;
    virtual bool guiParamDesc(uint32_t index, ParamDesc* desc) noexcept = 0;
    virtual double guiParamValue(clap_id paramId) noexcept = 0;
    virtual void guiParamValueText(clap_id paramId, double value, char* out,
                                   uint32_t capacity) noexcept = 0;

    virtual void guiBeginGesture(clap_id paramId) noexcept = 0;
    virtual void guiSetValue(clap_id paramId, double value) noexcept = 0;
    virtual void guiEndGesture(clap_id paramId) noexcept = 0;

    virtual LogBuffer& guiLog() noexcept = 0;

    // Latest transport snapshot. [any-thread]
    virtual TransportInfo guiTransport() noexcept = 0;

    // Host-contract badge: total violations observed and the most recent
    // stable code ("L03"); returns false while none occurred. [any-thread]
    virtual uint32_t guiViolationTotal() noexcept = 0;
    virtual bool guiLastViolation(char* buf, uint32_t capacity) noexcept = 0;
};

} // namespace cvp
