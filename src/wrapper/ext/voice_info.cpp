#include "wrapper/ext/voice_info.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

bool sGet(const clap_plugin* p, clap_voice_info* info) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return Plugin::from(p)->extensionProvider<VoiceInfoProvider>(CLAP_EXT_VOICE_INFO)->voiceInfo(info);
}

const clap_plugin_voice_info kVtable = {
    .get = sGet,
};

} // namespace

const clap_plugin_voice_info* voiceInfoVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
