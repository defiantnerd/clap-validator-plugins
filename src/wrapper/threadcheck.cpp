#include "wrapper/threadcheck.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "wrapper/logbuffer.h"

namespace cvp {

void ThreadChecker::onInit(const clap_host* host, LogBuffer* logBuffer) noexcept {
    _host = host;
    _logBuffer = logBuffer;
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

void ThreadChecker::report(const char* function, const char* expected,
                           bool authoritative) const noexcept {
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "clap-validator-plugin: '%s' was called outside the [%s-thread] contract%s",
                  function, expected,
                  authoritative ? "" : " (heuristic: host lacks clap.thread-check)");
    if (_logBuffer)
        _logBuffer->append(CLAP_LOG_HOST_MISBEHAVING, msg);
    if (_hostLog && _hostLog->log)
        _hostLog->log(_host, CLAP_LOG_HOST_MISBEHAVING, msg);
    else
        std::fprintf(stderr, "%s\n", msg);
    if (authoritative)
        assert(false && "CLAP thread contract violated by host");
}

} // namespace cvp
