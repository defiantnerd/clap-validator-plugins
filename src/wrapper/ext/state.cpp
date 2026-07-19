#include "wrapper/ext/state.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

StateProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<StateProvider>(CLAP_EXT_STATE);
}

bool sSave(const clap_plugin* p, const clap_ostream* stream) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->stateSave(stream);
}

bool sLoad(const clap_plugin* p, const clap_istream* stream) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->stateLoad(stream);
}

const clap_plugin_state kVtable = {
    .save = sSave,
    .load = sLoad,
};

} // namespace

const clap_plugin_state* stateVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
