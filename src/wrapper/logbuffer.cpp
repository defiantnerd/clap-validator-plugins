#include "wrapper/logbuffer.h"

#include <chrono>
#include <cstdio>

namespace cvp {

const char* severityTag(clap_log_severity severity) noexcept {
    switch (severity) {
    case CLAP_LOG_DEBUG:
        return "DBG";
    case CLAP_LOG_INFO:
        return "INF";
    case CLAP_LOG_WARNING:
        return "WRN";
    case CLAP_LOG_ERROR:
        return "ERR";
    case CLAP_LOG_FATAL:
        return "FTL";
    case CLAP_LOG_HOST_MISBEHAVING:
        return "HOST!";
    case CLAP_LOG_PLUGIN_MISBEHAVING:
        return "PLUG!";
    default:
        return "???";
    }
}

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

void LogBuffer::append(clap_log_severity severity, const char* message) noexcept {
    const int64_t now = nowMs();
    char prefix[48];

    std::lock_guard<std::mutex> lock(_mutex);
    if (_epochMs < 0)
        _epochMs = now;
    const int64_t rel = now - _epochMs;
    std::snprintf(prefix, sizeof(prefix), "%4lld.%03lld [%-5s] ",
                  static_cast<long long>(rel / 1000), static_cast<long long>(rel % 1000),
                  severityTag(severity));
    _lines.emplace_back(std::string(prefix) + (message ? message : ""));
    if (_lines.size() > kMaxLines)
        _lines.pop_front();
    ++_version;
}

uint64_t LogBuffer::version() const noexcept {
    std::lock_guard<std::mutex> lock(_mutex);
    return _version;
}

std::vector<std::string> LogBuffer::snapshot() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return {_lines.begin(), _lines.end()};
}

} // namespace cvp
