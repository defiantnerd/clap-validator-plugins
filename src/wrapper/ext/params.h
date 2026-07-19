#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.params — implement and register with
// provideExtension(CLAP_EXT_PARAMS, paramsVtable(), static_cast<ParamsProvider*>(this)).
class ParamsProvider {
public:
    virtual ~ParamsProvider() = default;

    virtual uint32_t paramCount() noexcept = 0;                                 // [main]
    virtual bool paramInfo(uint32_t index, clap_param_info* info) noexcept = 0; // [main]
    virtual bool paramValue(clap_id paramId, double* value) noexcept = 0;       // [main]
    virtual bool paramValueToText(clap_id paramId, double value, char* out,
                                  uint32_t capacity) noexcept = 0;              // [main]
    virtual bool paramTextToValue(clap_id paramId, const char* text,
                                  double* value) noexcept = 0;                  // [main]
    // [audio-thread while active, main-thread otherwise]
    virtual void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept = 0;
};

const clap_plugin_params* paramsVtable() noexcept;

} // namespace cvp::ext
