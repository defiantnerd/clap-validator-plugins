#include "wrapper/ext/note_name.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

NoteNameProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<NoteNameProvider>(CLAP_EXT_NOTE_NAME);
}

uint32_t sCount(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->noteNameCount();
}

bool sGet(const clap_plugin* p, uint32_t index, clap_note_name* noteName) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->noteNameGet(index, noteName);
}

const clap_plugin_note_name kVtable = {
    .count = sCount,
    .get = sGet,
};

} // namespace

const clap_plugin_note_name* noteNameVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
