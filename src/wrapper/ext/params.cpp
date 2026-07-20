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
    auto* plugin = Plugin::from(p);
    auto& contract = plugin->contract();
    // Must never overlap a running process() call (params.h).
    if (contract.isInProcess())
        contract.report(Violation::P07, "params.flush()", "called concurrently with process()");
    // [active ? audio-thread : main-thread] — only decidable when the host
    // provides clap.thread-check; heuristics would misfire here.
    if (contract.isActive()) {
        if (plugin->threadChecker().confirmedNotAudioThread())
            contract.report(Violation::T03, "params.flush()",
                            "must be on the audio thread while the plugin is active");
    } else if (plugin->threadChecker().confirmedNotMainThread()) {
        contract.report(Violation::T03, "params.flush()",
                        "must be on the main thread while the plugin is inactive");
    }
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
