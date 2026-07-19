#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.voice-info — implement and register with
// provideExtension(CLAP_EXT_VOICE_INFO, voiceInfoVtable(), static_cast<VoiceInfoProvider*>(this)).
class VoiceInfoProvider {
public:
    virtual ~VoiceInfoProvider() = default;

    virtual bool voiceInfo(clap_voice_info* info) noexcept = 0; // [main]
};

const clap_plugin_voice_info* voiceInfoVtable() noexcept;

} // namespace cvp::ext
