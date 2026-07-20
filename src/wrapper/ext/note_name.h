#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.note-name — implement and register with
// provideExtension(CLAP_EXT_NOTE_NAME, noteNameVtable(), static_cast<NoteNameProvider*>(this)).
class NoteNameProvider {
public:
    virtual ~NoteNameProvider() = default;

    virtual uint32_t noteNameCount() noexcept = 0;                            // [main]
    virtual bool noteNameGet(uint32_t index, clap_note_name* noteName) noexcept = 0; // [main]
};

const clap_plugin_note_name* noteNameVtable() noexcept;

} // namespace cvp::ext
