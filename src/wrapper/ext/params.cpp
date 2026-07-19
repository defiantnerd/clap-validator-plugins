#include "wrapper/ext/params.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

ParamsProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<ParamsProvider>(CLAP_EXT_PARAMS);
}

uint32_t sCount(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->paramCount();
}

bool sGetInfo(const clap_plugin* p, uint32_t index, clap_param_info* info) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->paramInfo(index, info);
}

bool sGetValue(const clap_plugin* p, clap_id paramId, double* value) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->paramValue(paramId, value);
}

bool sValueToText(const clap_plugin* p, clap_id paramId, double value, char* out,
                  uint32_t capacity) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->paramValueToText(paramId, value, out, capacity);
}

bool sTextToValue(const clap_plugin* p, clap_id paramId, const char* text, double* value) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->paramTextToValue(paramId, text, value);
}

void sFlush(const clap_plugin* p, const clap_input_events* in, const clap_output_events* out) {
    // Contract is [audio-thread while active, main-thread otherwise]; neither
    // side is assertable without knowing the host's intent, so no check here.
    provider(p)->paramsFlush(in, out);
}

const clap_plugin_params kVtable = {
    .count = sCount,
    .get_info = sGetInfo,
    .get_value = sGetValue,
    .value_to_text = sValueToText,
    .text_to_value = sTextToValue,
    .flush = sFlush,
};

} // namespace

const clap_plugin_params* paramsVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
