#include "wrapper/ext/tail.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

uint32_t sGet(const clap_plugin* p) {
    // [main-thread, audio-thread] — either is legal, but a third-party thread
    // is not (T04). Only decidable when the host provides clap.thread-check.
    auto* plugin = Plugin::from(p);
    if (plugin->threadChecker().confirmedNotMainThread() &&
        plugin->threadChecker().confirmedNotAudioThread())
        plugin->contract().report(Violation::T04, "tail.get()",
                                  "called on a thread that is neither main nor audio");
    return plugin->extensionProvider<TailProvider>(CLAP_EXT_TAIL)->tail();
}

const clap_plugin_tail kVtable = {
    .get = sGet,
};

} // namespace

const clap_plugin_tail* tailVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
