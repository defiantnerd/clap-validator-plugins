#pragma once

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cvp {

class LogBuffer;

// Stable host-contract violation codes. One entry per detectable rule from
// docs/host-contract-violations.md; the short string ("L03") is what appears
// in log lines, the GUI badge and the org.clap-validator.violations query
// extension, so it must never be renumbered.
//
// Entry-level findings (E01..E04, PD01) happen before/without a plugin
// instance and are carried by EarlyLog instead — they have no per-instance
// counter here.
#define CVP_VIOLATION_CODES(X)                                                                     \
    X(L01) X(L02) X(L03) X(L04) X(L05) X(L06) X(L07) X(L08) X(L09) X(L10) X(L11) X(L12) X(L13)     \
    X(P01) X(P02) X(P03) X(P04) X(P05) X(P06) X(P07) X(P08)                                        \
    X(T01) X(T02) X(T03) X(T04)                                                                    \
    X(G01) X(G02) X(G03) X(G04)                                                                    \
    X(AP01) X(NP01) X(AC01) X(CA01) X(LT01) X(VI01) X(R01) X(PL01) X(SR01)

enum class Violation : uint8_t {
#define CVP_X(code) code,
    CVP_VIOLATION_CODES(CVP_X)
#undef CVP_X
        Count_
};

const char* violationCode(Violation v) noexcept;

// Per-instance host-contract monitor: the single source of truth for the
// plugin's lifecycle state (readable from any thread) plus per-code violation
// counters feeding the log, the GUI badge, the destroy-time summary and the
// query extension. Sequence violations never assert — the plugin's job is to
// report host misbehavior, not to die from it.
class ContractMonitor {
public:
    static constexpr size_t kCodeCount = static_cast<size_t>(Violation::Count_);
    static constexpr size_t kMessageCapacity = 256;

    // logBuffer must outlive the monitor (both live in Plugin).
    void setup(const clap_host* host, LogBuffer* logBuffer) noexcept;
    // Called from clap_plugin.init() once the host log extension is known.
    void onHostLog(const clap_host_log* hostLog) noexcept { _hostLog = hostLog; }

    // --- lifecycle state (single source; Plugin accessors delegate here) ---
    void setInitialized() noexcept { _initialized.store(true, std::memory_order_release); }
    bool isInitialized() const noexcept { return _initialized.load(std::memory_order_acquire); }
    void setActive(bool active) noexcept { _active.store(active, std::memory_order_release); }
    bool isActive() const noexcept { return _active.load(std::memory_order_acquire); }
    void setProcessing(bool processing) noexcept {
        _processing.store(processing, std::memory_order_release);
    }
    bool isProcessing() const noexcept { return _processing.load(std::memory_order_acquire); }

    // [being-activated] window: spans the clap_plugin.activate() call.
    void beginActivation() noexcept { _beingActivated.store(true, std::memory_order_release); }
    void endActivation() noexcept { _beingActivated.store(false, std::memory_order_release); }
    bool isBeingActivated() const noexcept {
        return _beingActivated.load(std::memory_order_acquire);
    }

    // Concurrency guard for process(); false means another process() call is
    // still running (L12 — report and bail out without touching state).
    bool tryEnterProcess() noexcept {
        bool expected = false;
        return _inProcess.compare_exchange_strong(expected, true, std::memory_order_acquire);
    }
    void leaveProcess() noexcept { _inProcess.store(false, std::memory_order_release); }
    bool isInProcess() const noexcept { return _inProcess.load(std::memory_order_acquire); }

    // --- GUI lifecycle (G01..G04) ---
    void setGuiCreated(bool created, bool floating) noexcept {
        _guiFloating.store(floating, std::memory_order_relaxed);
        _guiCreated.store(created, std::memory_order_release);
    }
    bool guiCreated() const noexcept { return _guiCreated.load(std::memory_order_acquire); }
    bool guiFloating() const noexcept { return _guiFloating.load(std::memory_order_relaxed); }

    // --- activate-time caches for the process() pre-pass ---
    void setFrameBounds(uint32_t minFrames, uint32_t maxFrames) noexcept {
        _minFrames = minFrames;
        _maxFrames = maxFrames;
    }
    uint32_t minFrames() const noexcept { return _minFrames; }
    uint32_t maxFrames() const noexcept { return _maxFrames; }

    // Channel count per declared port, captured at activate (the layout must
    // not change while active, so audio-thread reads are safe).
    void setPortLayout(std::vector<uint32_t> inputChannels,
                       std::vector<uint32_t> outputChannels) noexcept {
        _inputChannels = std::move(inputChannels);
        _outputChannels = std::move(outputChannels);
    }
    const std::vector<uint32_t>& inputChannels() const noexcept { return _inputChannels; }
    const std::vector<uint32_t>& outputChannels() const noexcept { return _outputChannels; }

    // Valid param ids, captured at activate for the P08 event check (same
    // stability argument as the port layout).
    void setParamIds(std::vector<clap_id> ids) noexcept { _paramIds = std::move(ids); }
    bool hasParams() const noexcept { return !_paramIds.empty(); }
    bool knownParamId(clap_id id) const noexcept {
        for (clap_id known : _paramIds)
            if (known == id)
                return true;
        return false;
    }

    // --- steady_time monotonicity (P02); audio-thread only ---
    void invalidateSteadyTime() noexcept { _steadyValid = false; }
    // Returns false if the new steady_time violates monotonicity.
    bool checkSteadyTime(int64_t steadyTime, uint32_t framesCount) noexcept {
        if (steadyTime < 0) { // -1: not available
            _steadyValid = false;
            return true;
        }
        const bool ok = !_steadyValid || steadyTime >= _steadyEnd;
        _steadyValid = true;
        _steadyEnd = steadyTime + framesCount;
        return ok;
    }

    // --- on_main_thread pairing (L11) ---
    void noteCallbackRequested() noexcept {
        _pendingCallbacks.fetch_add(1, std::memory_order_relaxed);
    }
    // Returns false if no request_callback() was pending (spurious callback).
    bool consumePendingCallbacks() noexcept {
        // Hosts may coalesce several requests into one callback: drain all.
        return _pendingCallbacks.exchange(0, std::memory_order_relaxed) > 0;
    }

    // --- reporting ---
    // Counts the violation and emits a throttled log line:
    //   "seq [L03] activate(): called while already active (active=1)"
    // Occurrences 1..5 are logged in full, then every 100th as a repeat note;
    // counters always increment.
    void report(Violation v, const char* function, const char* detail) noexcept;
    // Counts and records without emitting — for paths that already produced
    // their own log line (ThreadChecker keeps its established wording).
    void note(Violation v, const char* message) noexcept;

    // Destroy-time scoreboard, e.g. "contract summary: 3 violations (L03×1,
    // P02×2)" — HOST_MISBEHAVING when non-zero, INFO otherwise.
    void logSummary() noexcept;

    // --- query-extension backing (org.clap-validator.violations) ---
    uint32_t total() const noexcept { return _total.load(std::memory_order_relaxed); }
    uint32_t distinct() const noexcept;
    // index enumerates codes with a non-zero count, in enum order.
    bool getEntry(uint32_t index, char code[8], uint32_t* count,
                  char message[kMessageCapacity]) const noexcept;
    void clear() noexcept;

    // Most recent violation for the GUI badge: "L03" (returns false if none).
    bool lastViolation(char* buf, size_t size) const noexcept;

private:
    void emit(clap_log_severity severity, const char* line) noexcept;
    void record(Violation v, const char* line, uint32_t occurrence) noexcept;

    const clap_host* _host = nullptr;
    const clap_host_log* _hostLog = nullptr;
    LogBuffer* _logBuffer = nullptr;

    std::atomic<bool> _initialized{false};
    std::atomic<bool> _active{false};
    std::atomic<bool> _processing{false};
    std::atomic<bool> _beingActivated{false};
    std::atomic<bool> _inProcess{false};
    std::atomic<bool> _guiCreated{false};
    std::atomic<bool> _guiFloating{false};

    uint32_t _minFrames = 0;
    uint32_t _maxFrames = 0;
    std::vector<uint32_t> _inputChannels;
    std::vector<uint32_t> _outputChannels;
    std::vector<clap_id> _paramIds;

    bool _steadyValid = false; // audio-thread only
    int64_t _steadyEnd = 0;    // audio-thread only

    std::atomic<uint32_t> _pendingCallbacks{0};

    std::atomic<uint32_t> _counters[kCodeCount]{};
    std::atomic<uint32_t> _total{0};
    std::atomic<int> _lastCode{-1};
    mutable std::mutex _messageMutex;
    char _lastMessages[kCodeCount][kMessageCapacity]{};
};

} // namespace cvp
