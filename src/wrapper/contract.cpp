#include "wrapper/contract.h"

#include <cstdio>
#include <cstring>

#include "wrapper/logbuffer.h"

namespace cvp {

namespace {

constexpr uint32_t kFullReports = 5;   // occurrences logged verbatim
constexpr uint32_t kRepeatEvery = 100; // then every Nth as a repeat note

const char* const kCodeStrings[] = {
#define CVP_X(code) #code,
    CVP_VIOLATION_CODES(CVP_X)
#undef CVP_X
};

} // namespace

const char* violationCode(Violation v) noexcept {
    return kCodeStrings[static_cast<size_t>(v)];
}

void ContractMonitor::setup(const clap_host* host, LogBuffer* logBuffer) noexcept {
    _host = host;
    _logBuffer = logBuffer;
}

void ContractMonitor::emit(clap_log_severity severity, const char* line) noexcept {
    if (_logBuffer)
        _logBuffer->append(severity, line);
    if (_hostLog && _hostLog->log)
        _hostLog->log(_host, severity, line);
    else
        std::fprintf(stderr, "[clap-validator-plugin] [%s] %s\n", severityTag(severity), line);
}

void ContractMonitor::record(Violation v, const char* line, uint32_t occurrence) noexcept {
    const auto i = static_cast<size_t>(v);
    _total.fetch_add(1, std::memory_order_relaxed);
    _lastCode.store(static_cast<int>(i), std::memory_order_relaxed);
    if (occurrence <= kFullReports) { // later repeats keep the first messages
        std::lock_guard<std::mutex> lock(_messageMutex);
        std::snprintf(_lastMessages[i], kMessageCapacity, "%s", line);
    }
}

void ContractMonitor::report(Violation v, const char* function, const char* detail) noexcept {
    const auto i = static_cast<size_t>(v);
    const uint32_t n = _counters[i].fetch_add(1, std::memory_order_relaxed) + 1;
    char line[kMessageCapacity + 64];
    std::snprintf(line, sizeof(line), "seq [%s] %s: %s", violationCode(v), function, detail);
    record(v, line, n);
    if (n <= kFullReports) {
        emit(CLAP_LOG_HOST_MISBEHAVING, line);
    } else if (n % kRepeatEvery == 0) {
        std::snprintf(line, sizeof(line), "seq [%s] %s: repeated (total %u)", violationCode(v),
                      function, n);
        emit(CLAP_LOG_HOST_MISBEHAVING, line);
    }
}

void ContractMonitor::note(Violation v, const char* message) noexcept {
    const auto i = static_cast<size_t>(v);
    const uint32_t n = _counters[i].fetch_add(1, std::memory_order_relaxed) + 1;
    record(v, message, n);
}

void ContractMonitor::logSummary() noexcept {
    const uint32_t total = _total.load(std::memory_order_relaxed);
    if (total == 0) {
        emit(CLAP_LOG_INFO, "contract summary: no host contract violations observed");
        return;
    }
    char line[512];
    int pos = std::snprintf(line, sizeof(line), "contract summary: %u violation%s (", total,
                            total == 1 ? "" : "s");
    bool first = true;
    for (size_t i = 0; i < kCodeCount && pos < static_cast<int>(sizeof(line)) - 16; ++i) {
        const uint32_t count = _counters[i].load(std::memory_order_relaxed);
        if (count == 0)
            continue;
        pos += std::snprintf(line + pos, sizeof(line) - pos, "%s%s×%u", first ? "" : ", ",
                             kCodeStrings[i], count);
        first = false;
    }
    std::snprintf(line + pos, sizeof(line) - pos, ")");
    emit(CLAP_LOG_HOST_MISBEHAVING, line);
}

uint32_t ContractMonitor::distinct() const noexcept {
    uint32_t distinct = 0;
    for (const auto& counter : _counters)
        if (counter.load(std::memory_order_relaxed) > 0)
            ++distinct;
    return distinct;
}

bool ContractMonitor::getEntry(uint32_t index, char code[8], uint32_t* count,
                               char message[kMessageCapacity]) const noexcept {
    uint32_t seen = 0;
    for (size_t i = 0; i < kCodeCount; ++i) {
        const uint32_t n = _counters[i].load(std::memory_order_relaxed);
        if (n == 0)
            continue;
        if (seen++ == index) {
            std::snprintf(code, 8, "%s", kCodeStrings[i]);
            *count = n;
            std::lock_guard<std::mutex> lock(_messageMutex);
            std::memcpy(message, _lastMessages[i], kMessageCapacity);
            return true;
        }
    }
    return false;
}

void ContractMonitor::clear() noexcept {
    for (auto& counter : _counters)
        counter.store(0, std::memory_order_relaxed);
    _total.store(0, std::memory_order_relaxed);
    _lastCode.store(-1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(_messageMutex);
    std::memset(_lastMessages, 0, sizeof(_lastMessages));
}

bool ContractMonitor::lastViolation(char* buf, size_t size) const noexcept {
    const int last = _lastCode.load(std::memory_order_relaxed);
    if (last < 0)
        return false;
    std::snprintf(buf, size, "%s", kCodeStrings[last]);
    return true;
}

} // namespace cvp
