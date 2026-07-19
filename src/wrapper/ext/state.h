#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.state — implement and register with
// provideExtension(CLAP_EXT_STATE, stateVtable(), static_cast<StateProvider*>(this)).
class StateProvider {
public:
    virtual ~StateProvider() = default;

    virtual bool stateSave(const clap_ostream* stream) noexcept = 0; // [main]
    virtual bool stateLoad(const clap_istream* stream) noexcept = 0; // [main]
};

const clap_plugin_state* stateVtable() noexcept;

} // namespace cvp::ext
