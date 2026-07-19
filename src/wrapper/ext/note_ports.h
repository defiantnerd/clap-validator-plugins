#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.note-ports — implement and register with
// provideExtension(CLAP_EXT_NOTE_PORTS, notePortsVtable(), static_cast<NotePortsProvider*>(this)).
class NotePortsProvider {
public:
    virtual ~NotePortsProvider() = default;

    virtual uint32_t notePortCount(bool isInput) noexcept = 0; // [main]
    virtual bool notePortInfo(uint32_t index, bool isInput,
                              clap_note_port_info* info) noexcept = 0; // [main]
};

const clap_plugin_note_ports* notePortsVtable() noexcept;

} // namespace cvp::ext
