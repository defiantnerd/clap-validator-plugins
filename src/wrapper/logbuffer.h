#pragma once

#include <clap/clap.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace cvp {

// Short human-readable tag for a CLAP log severity ("INF", "WRN", "HOST!"...).
// Shared by the log ring and the stderr fallbacks so severity survives even
// when the host implements no clap.log.
const char* severityTag(clap_log_severity severity) noexcept;

// Per-plugin-instance log ring. Every logging path of the plugin flows
// through here FIRST (then on to the host's clap.log extension or stderr),
// so a GUI log view is guaranteed to see all output.
//
// Callable from any thread. The mutex on the audio path is a deliberate
// trade-off for a diagnostic/validator plugin; the critical section is a
// push/pop/counter bump only.
class LogBuffer {
public:
    // Appends a formatted line: relative timestamp + severity tag + message.
    void append(clap_log_severity severity, const char* message) noexcept;

    // Cheap change detection for polling UIs: bumped on every append.
    uint64_t version() const noexcept;

    // Copies the current lines out (render outside the lock).
    std::vector<std::string> snapshot() const;

private:
    static constexpr size_t kMaxLines = 400;

    mutable std::mutex _mutex;
    std::deque<std::string> _lines;
    uint64_t _version = 0;
    int64_t _epochMs = -1; // set on first append; timestamps are relative to it
};

} // namespace cvp
