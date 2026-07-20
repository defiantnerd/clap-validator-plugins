#include "plugins/params_zoo/params_zoo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr uint32_t kStateMagic = 0x43565050; // "CVPP"
constexpr uint32_t kStateVersion = 1;
constexpr double kPi = 3.14159265358979323846;

const char* const kCurveNames[] = {"Linear", "Soft", "Hard"};

double shape(double x, double drive, int curve) {
    if (drive <= 0.0)
        return x;
    const double boosted = x * (1.0 + drive);
    switch (curve) {
    case 1: // Soft: tanh saturation
        return std::tanh(boosted);
    case 2: // Hard: clip
        return std::clamp(boosted, -1.0, 1.0);
    default: // Linear: plain gain from drive
        return boosted;
    }
}

} // namespace

const clap_plugin_descriptor ParamsZooPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.params",
    .name = "Validator Params",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugins",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugins/issues",
    .version = "0.1.0",
    .description =
        "Parameter-type zoo: automatable+modulatable (with non-null cookie), "
        "non-automatable, read-only meter, bypass, periodic, requires-process, stepped enum "
        "and hidden parameters in one plugin.",
    .features = kFeatures,
};

const clap_plugin* ParamsZooPlugin::create(const clap_host* host) {
    return (new ParamsZooPlugin(host))->clapPlugin();
}

ParamsZooPlugin::ParamsZooPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    provideExtension(CLAP_EXT_REMOTE_CONTROLS_COMPAT, ext::remoteControlsVtable(),
                     static_cast<ext::RemoteControlsProvider*>(this));
    // Deliberately NOT: note-ports, latency, tail, render.
}

// ---- audio-ports ----

uint32_t ParamsZooPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool ParamsZooPlugin::audioPortInfo(uint32_t index, bool isInput,
                                    clap_audio_port_info* info) noexcept {
    if (index != 0)
        return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0;
    return true;
}

// ---- params ----

std::atomic<double>* ParamsZooPlugin::paramStorage(clap_id paramId) noexcept {
    switch (paramId) {
    case kParamGain:
        return &_gainDb;
    case kParamDrive:
        return &_drive;
    case kParamPeak:
        return &_peak;
    case kParamBypass:
        return &_bypass;
    case kParamRotation:
        return &_rotationDeg;
    case kParamDcOffset:
        return &_dcOffset;
    case kParamCurve:
        return &_curve;
    case kParamSecret:
        return &_secretTrim;
    default:
        return nullptr;
    }
}

uint32_t ParamsZooPlugin::paramCount() noexcept {
    return kParamCount;
}

bool ParamsZooPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index >= kParamCount)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->cookie = nullptr;
    std::snprintf(info->module, sizeof(info->module), "%s", "");
    switch (index) {
    case 0:
        info->id = kParamGain;
        std::snprintf(info->name, sizeof(info->name), "Gain");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
        // The only non-null cookie in the suite: hosts using the cookie fast
        // path must hand exactly this pointer back in events (else P11).
        info->cookie = &_gainDb;
        info->min_value = -24.0;
        info->max_value = 24.0;
        info->default_value = 0.0;
        break;
    case 1:
        info->id = kParamDrive;
        std::snprintf(info->name, sizeof(info->name), "Drive");
        // flags = 0: no automation record/playback, but live host changes are
        // explicitly allowed by the spec ("The host can send live user
        // changes for this parameter regardless of this flag").
        info->flags = 0;
        info->min_value = 0.0;
        info->max_value = 10.0;
        info->default_value = 0.0;
        break;
    case 2:
        info->id = kParamPeak;
        std::snprintf(info->name, sizeof(info->name), "In Peak");
        // Reads the INPUT level: a self-updating read-only meter that stays
        // at its default on silent input, which keeps state round-trips
        // deterministic (clap-validator processes silence in its state tests
        // and expects every param — read-only included — to round-trip).
        info->flags = CLAP_PARAM_IS_READONLY;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        break;
    case 3:
        info->id = kParamBypass;
        std::snprintf(info->name, sizeof(info->name), "Bypass");
        info->flags = CLAP_PARAM_IS_BYPASS | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        break;
    case 4:
        info->id = kParamRotation;
        std::snprintf(info->name, sizeof(info->name), "Rotation");
        info->flags = CLAP_PARAM_IS_PERIODIC | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0.0;
        info->max_value = 360.0;
        info->default_value = 0.0;
        break;
    case 5:
        info->id = kParamDcOffset;
        std::snprintf(info->name, sizeof(info->name), "DC Offset");
        // The params.h example for REQUIRES_PROCESS is literally a DC offset.
        info->flags = CLAP_PARAM_REQUIRES_PROCESS | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = -0.2;
        info->max_value = 0.2;
        info->default_value = 0.0;
        break;
    case 6:
        info->id = kParamCurve;
        std::snprintf(info->name, sizeof(info->name), "Curve");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        info->min_value = 0.0;
        info->max_value = 2.0;
        info->default_value = 0.0;
        break;
    case 7:
        info->id = kParamSecret;
        std::snprintf(info->name, sizeof(info->name), "Secret Trim");
        info->flags = CLAP_PARAM_IS_HIDDEN;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 1.0;
        break;
    }
    return true;
}

bool ParamsZooPlugin::paramValue(clap_id paramId, double* value) noexcept {
    const auto* storage = paramStorage(paramId);
    if (!storage)
        return false;
    *value = storage->load(std::memory_order_relaxed);
    return true;
}

bool ParamsZooPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                       uint32_t capacity) noexcept {
    switch (paramId) {
    case kParamGain:
        std::snprintf(out, capacity, "%.1f dB", value);
        return true;
    case kParamDrive:
        std::snprintf(out, capacity, "%.1f", value);
        return true;
    case kParamPeak:
        std::snprintf(out, capacity, "%.3f", value);
        return true;
    case kParamBypass:
        std::snprintf(out, capacity, "%s", value >= 0.5 ? "Bypassed" : "Active");
        return true;
    case kParamRotation:
        std::snprintf(out, capacity, "%.0f°", value);
        return true;
    case kParamDcOffset:
        std::snprintf(out, capacity, "%+.3f", value);
        return true;
    case kParamCurve: {
        const int index = std::clamp(static_cast<int>(value), 0, 2);
        std::snprintf(out, capacity, "%s", kCurveNames[index]); // every enum value has text
        return true;
    }
    case kParamSecret:
        std::snprintf(out, capacity, "%.2f", value);
        return true;
    default:
        return false;
    }
}

bool ParamsZooPlugin::paramTextToValue(clap_id paramId, const char* text, double* value) noexcept {
    if (!paramStorage(paramId))
        return false;
    if (paramId == kParamCurve) {
        for (int i = 0; i < 3; ++i)
            if (std::strcmp(text, kCurveNames[i]) == 0) {
                *value = i;
                return true;
            }
    }
    if (paramId == kParamBypass) {
        if (std::strcmp(text, "Active") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(text, "Bypassed") == 0) {
            *value = 1.0;
            return true;
        }
    }
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text)
        return false;
    *value = parsed;
    return true;
}

void ParamsZooPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;

    // Contract checks (wrong cookie P11, read-only write P12, illegal
    // modulation P13, automated non-automatable P14) run generically in the
    // Plugin base — this only APPLIES legal changes.
    if (header->type == CLAP_EVENT_PARAM_MOD) {
        const auto* event = reinterpret_cast<const clap_event_param_mod*>(header);
        // Modulation is an additive offset on top of the value; it is never
        // persisted and not reflected by get_value(). Only Gain is
        // modulatable; other targets are ignored (and reported as P13).
        if (event->param_id == kParamGain)
            _gainModDb.store(event->amount, std::memory_order_relaxed);
        return;
    }

    if (header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    if (event->param_id == kParamPeak)
        return; // read-only: the write is ignored (reported as P12)
    if (auto* storage = paramStorage(event->param_id))
        storage->store(event->value, std::memory_order_relaxed);
}

void ParamsZooPlugin::paramsFlush(const clap_input_events* in, const clap_output_events*) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
}

// ---- state ----

bool ParamsZooPlugin::stateSave(const clap_ostream* stream) noexcept {
    if (!streamWrite(stream, kStateMagic) || !streamWrite(stream, kStateVersion))
        return false;
    // Peak is a read-only meter and Gain modulation is a live signal: neither
    // belongs in the state.
    const clap_id saved[] = {kParamGain,     kParamDrive, kParamBypass, kParamRotation,
                             kParamDcOffset, kParamCurve, kParamSecret};
    if (!streamWrite(stream, static_cast<uint32_t>(sizeof(saved) / sizeof(saved[0]))))
        return false;
    for (clap_id id : saved) {
        const double value = paramStorage(id)->load(std::memory_order_relaxed);
        if (!streamWrite(stream, static_cast<uint32_t>(id)) || !streamWrite(stream, value))
            return false;
    }
    return true;
}

bool ParamsZooPlugin::stateLoad(const clap_istream* stream) noexcept {
    uint32_t magic = 0, version = 0, count = 0;
    if (!streamRead(stream, magic) || magic != kStateMagic)
        return false;
    if (!streamRead(stream, version) || version != kStateVersion)
        return false;
    if (!streamRead(stream, count))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        double value = 0.0;
        if (!streamRead(stream, id) || !streamRead(stream, value))
            return false;
        if (id == kParamPeak)
            continue; // never restore the meter
        if (auto* storage = paramStorage(id))
            storage->store(value, std::memory_order_relaxed);
    }
    return true;
}

// ---- remote-controls ----

uint32_t ParamsZooPlugin::remoteControlsPageCount() noexcept {
    return 1;
}

bool ParamsZooPlugin::remoteControlsPage(uint32_t pageIndex,
                                         clap_remote_controls_page* page) noexcept {
    if (pageIndex != 0)
        return false;
    const clap_id params[] = {kParamGain, kParamDrive, kParamBypass, kParamRotation,
                              kParamDcOffset, kParamCurve};
    ext::fillRemoteControlsPage(page, 0, "Validator", "Zoo", params, 6);
    return true;
}

// ---- processing ----

clap_process_status ParamsZooPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;

    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            applyParamEvent(process->in_events->get(process->in_events, i));
    }

    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    if (!in.data32 || !out.data32)
        return CLAP_PROCESS_ERROR;
    const float* srcL = in.data32[0];
    const float* srcR = in.channel_count > 1 ? in.data32[1] : in.data32[0];
    float* dstL = out.data32[0];
    float* dstR = out.channel_count > 1 ? out.data32[1] : nullptr;

    // The read-only meter measures the INPUT peak (with per-block decay), so
    // it works in bypass too and never moves on silent input.
    {
        double peak = _peak.load(std::memory_order_relaxed) * 0.95;
        for (uint32_t i = 0; i < frames; ++i)
            peak = std::max({peak, static_cast<double>(std::fabs(srcL[i])),
                             static_cast<double>(std::fabs(srcR[i]))});
        _peak.store(std::min(peak, 1.0), std::memory_order_relaxed);
    }

    // Bypass still processes (per the IS_BYPASS doc the host keeps calling
    // process); it just copies input to output.
    if (_bypass.load(std::memory_order_relaxed) >= 0.5) {
        for (uint32_t i = 0; i < frames; ++i) {
            dstL[i] = srcL[i];
            if (dstR)
                dstR[i] = srcR[i];
        }
        return CLAP_PROCESS_CONTINUE;
    }

    const double gainDb = _gainDb.load(std::memory_order_relaxed) +
                          _gainModDb.load(std::memory_order_relaxed); // mod = additive offset
    const double gain = std::pow(10.0, gainDb / 20.0);
    const double drive = _drive.load(std::memory_order_relaxed);
    const int curve = std::clamp(static_cast<int>(_curve.load(std::memory_order_relaxed)), 0, 2);
    const double dc = _dcOffset.load(std::memory_order_relaxed);
    const double trim = _secretTrim.load(std::memory_order_relaxed);
    const double angle = _rotationDeg.load(std::memory_order_relaxed) * kPi / 180.0;
    const double rotCos = std::cos(angle);
    const double rotSin = std::sin(angle);

    for (uint32_t i = 0; i < frames; ++i) {
        const double left = shape(srcL[i] * gain * trim, drive, curve) + dc;
        const double right = shape(srcR[i] * gain * trim, drive, curve) + dc;
        // Stereo rotation: at 0° identity, wraps cleanly at 360° (periodic).
        dstL[i] = static_cast<float>(left * rotCos - right * rotSin);
        if (dstR)
            dstR[i] = static_cast<float>(left * rotSin + right * rotCos);
    }

    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
