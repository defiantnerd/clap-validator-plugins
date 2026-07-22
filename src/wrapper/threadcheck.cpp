#include "wrapper/threadcheck.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "wrapper/contract.h"
#include "wrapper/logbuffer.h"

namespace cvp {

void ThreadChecker::onInit(const clap_host* host, LogBuffer* logBuffer,
                           ContractMonitor* monitor) noexcept {
    _host = host;
    _logBuffer = logBuffer;
    _monitor = monitor;
    _mainThread = std::this_thread::get_id();
    if (host) {
        _hostCheck =
            static_cast<const clap_host_thread_check*>(host->get_extension(host, CLAP_EXT_THREAD_CHECK));
        _hostLog = static_cast<const clap_host_log*>(host->get_extension(host, CLAP_EXT_LOG));
    }
}

void ThreadChecker::assertMainThread(const char* function) const noexcept {
    if (_hostCheck && _hostCheck->is_main_thread) {
        if (!_hostCheck->is_main_thread(_host))
            report(function, "main", true);
        return;
    }
    if (std::this_thread::get_id() != _mainThread)
        report(function, "main", false);
}

void ThreadChecker::assertAudioThread(const char* function) const noexcept {
    if (_hostCheck && _hostCheck->is_audio_thread) {
        if (!_hostCheck->is_audio_thread(_host))
            report(function, "audio", true);
        return;
    }
    // Without clap.thread-check there is no reliable way to identify the
    // audio thread; skip rather than produce false positives.
}

bool ThreadChecker::confirmedNotMainThread() const noexcept {
    return _hostCheck && _hostCheck->is_main_thread && !_hostCheck->is_main_thread(_host);
}

bool ThreadChecker::confirmedNotAudioThread() const noexcept {
    return _hostCheck && _hostCheck->is_audio_thread && !_hostCheck->is_audio_thread(_host);
}

void ThreadChecker::report(const char* function, const char* expected,
                           bool authoritative) const noexcept {
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "clap-validator-plugin: '%s' was called outside the [%s-thread] contract%s",
                  function, expected,
                  authoritative ? "" : " (heuristic: host lacks clap.thread-check)");
    // A heuristic mismatch may be a legal host choice — only an authoritative
    // clap.thread-check answer justifies the HOST_MISBEHAVING severity.
    const clap_log_severity severity =
        authoritative ? CLAP_LOG_HOST_MISBEHAVING : CLAP_LOG_WARNING;
    if (_logBuffer)
        _logBuffer->append(severity, msg);
    if (_hostLog && _hostLog->log)
        _hostLog->log(_host, severity, msg);
    else
        std::fprintf(stderr, "[clap-validator-plugin] [%s] %s\n", severityTag(severity), msg);
    // Only authoritative findings enter the violation registry — a heuristic
    // mismatch may be a legal host choice, and the badge must not lie.
    if (authoritative && _monitor)
        _monitor->note(std::strcmp(expected, "main") == 0 ? Violation::T01 : Violation::T02, msg);
    if (authoritative)
        assert(false && "CLAP thread contract violated by host");
}

} // namespace cvp
