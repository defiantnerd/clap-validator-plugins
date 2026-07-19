#pragma once

#include <clap/clap.h>

#include <thread>

namespace cvp {

// Enforces CLAP's [main-thread]/[audio-thread] contracts at runtime.
//
// Prefers the host's clap.thread-check extension (authoritative); without it,
// main-thread checks compare against the thread that ran clap_plugin.init()
// and audio-thread checks are skipped (a host may legally run audio on any
// thread, including the main thread during offline rendering).
//
// Violations are reported through the host's clap.log extension with
// CLAP_LOG_HOST_MISBEHAVING, then assert() in debug builds.
class ThreadChecker {
public:
    void onInit(const clap_host* host) noexcept;

    void assertMainThread(const char* function) const noexcept;
    void assertAudioThread(const char* function) const noexcept;

private:
    // authoritative: the violation was confirmed by the host's own
    // clap.thread-check extension (assert-worthy). Heuristic findings from the
    // init-thread fallback are logged only — a host may legally drive the
    // plugin from a thread other than the one that happened to run init().
    void report(const char* function, const char* expected, bool authoritative) const noexcept;

    const clap_host* _host = nullptr;
    const clap_host_thread_check* _hostCheck = nullptr;
    const clap_host_log* _hostLog = nullptr;
    std::thread::id _mainThread{};
};

} // namespace cvp

#if defined(CVP_THREAD_CHECKS) && CVP_THREAD_CHECKS
#define CVP_ASSERT_MAIN_THREAD(self) (self)->threadChecker().assertMainThread(__func__)
#define CVP_ASSERT_AUDIO_THREAD(self) (self)->threadChecker().assertAudioThread(__func__)
#else
#define CVP_ASSERT_MAIN_THREAD(self) ((void)0)
#define CVP_ASSERT_AUDIO_THREAD(self) ((void)0)
#endif
