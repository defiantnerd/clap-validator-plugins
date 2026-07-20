#include "wrapper/earlylog.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "wrapper/logbuffer.h"

namespace cvp::earlylog {

namespace {

constexpr size_t kMaxLines = 64;
constexpr size_t kLineCapacity = 256;

struct Ring {
    std::mutex mutex;
    char lines[kMaxLines][kLineCapacity];
    clap_log_severity severities[kMaxLines];
    size_t count = 0; // saturates at kMaxLines; the earliest lines matter most
};

// Function-local static: safe to touch from entry.init() itself.
Ring& ring() {
    static Ring instance;
    return instance;
}

} // namespace

void append(clap_log_severity severity, const char* message) noexcept {
    std::fprintf(stderr, "[clap-validator-plugin] entry: %s\n", message);
    auto& r = ring();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.count >= kMaxLines)
        return;
    std::snprintf(r.lines[r.count], kLineCapacity, "entry: %s", message);
    r.severities[r.count] = severity;
    ++r.count;
}

void copyInto(LogBuffer& sink) noexcept {
    auto& r = ring();
    std::lock_guard<std::mutex> lock(r.mutex);
    for (size_t i = 0; i < r.count; ++i)
        sink.append(r.severities[i], r.lines[i]);
}

} // namespace cvp::earlylog
