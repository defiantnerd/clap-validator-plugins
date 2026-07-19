#include "plugins/effect/effect.h"

#include <cstdio>
#include <cstring>

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

} // namespace

const clap_plugin_descriptor EffectPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.effect",
    .name = "Validator Effect",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugin",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugin/issues",
    .version = "0.1.0",
    .description = "Stereo effect exposing audio-ports and deliberately no note-ports.",
    .features = kFeatures,
};

const clap_plugin* EffectPlugin::create(const clap_host* host) {
    return (new EffectPlugin(host))->clapPlugin();
}

EffectPlugin::EffectPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    // Deliberately NOT: CLAP_EXT_NOTE_PORTS.
}

uint32_t EffectPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool EffectPlugin::audioPortInfo(uint32_t index, bool isInput,
                                 clap_audio_port_info* info) noexcept {
    if (index != 0)
        return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0; // in-place processing with the matching port is fine
    return true;
}

clap_process_status EffectPlugin::process(const clap_process* process) noexcept {
    // M1: pure passthrough (gain param lands in M2).
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;

    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels = in.channel_count < out.channel_count ? in.channel_count
                                                                   : out.channel_count;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (out.data32[ch] != in.data32[ch])
            std::memcpy(out.data32[ch], in.data32[ch], frames * sizeof(float));
    }
    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
