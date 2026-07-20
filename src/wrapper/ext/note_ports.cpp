#include "wrapper/ext/note_ports.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

NotePortsProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<NotePortsProvider>(CLAP_EXT_NOTE_PORTS);
}

// Note-port scans are only legal while deactivated (note-ports.h) — NP01.
void checkNotActive(const clap_plugin* p, const char* function) {
    auto& contract = Plugin::from(p)->contract();
    if (contract.isActive() && !contract.isBeingActivated())
        contract.report(Violation::NP01, function,
                        "port scan while active (only legal when deactivated)");
}

uint32_t sCount(const clap_plugin* p, bool isInput) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    checkNotActive(p, "note_ports.count()");
    return provider(p)->notePortCount(isInput);
}

bool sGet(const clap_plugin* p, uint32_t index, bool isInput, clap_note_port_info* info) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    checkNotActive(p, "note_ports.get()");
    return provider(p)->notePortInfo(index, isInput, info);
}

const clap_plugin_note_ports kVtable = {
    .count = sCount,
    .get = sGet,
};

} // namespace

const clap_plugin_note_ports* notePortsVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
