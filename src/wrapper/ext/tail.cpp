#include "wrapper/ext/tail.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

uint32_t sGet(const clap_plugin* p) {
    // [main-thread, audio-thread] — either is legal, nothing to assert.
    return Plugin::from(p)->extensionProvider<TailProvider>(CLAP_EXT_TAIL)->tail();
}

const clap_plugin_tail kVtable = {
    .get = sGet,
};

} // namespace

const clap_plugin_tail* tailVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
