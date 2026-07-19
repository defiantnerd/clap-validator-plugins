#include "plugins/gui/gui_plugin.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wrapper/stream.h"

namespace cvp {

namespace {

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr uint32_t kStateMagic = 0x43565053; // "CVPS"
constexpr uint32_t kStateVersion = 1;

const char* kModeNames[] = {"Off", "Slow", "Medium", "Fast"};

#if defined(__APPLE__)
const char* kPlatformApi = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
const char* kPlatformApi = CLAP_WINDOW_API_WIN32;
#else
const char* kPlatformApi = CLAP_WINDOW_API_X11;
#endif

} // namespace

const clap_plugin_descriptor GuiPlugin::descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.clap-validator.gui",
    .name = "Validator GUI",
    .vendor = "CLAP Validator Plugin Project",
    .url = "https://github.com/defiantnerd/clap-validator-plugin",
    .manual_url = "",
    .support_url = "https://github.com/defiantnerd/clap-validator-plugin/issues",
    .version = "0.1.0",
    .description = "Stereo gain effect with a framework-free native editor: editable parameters "
                   "on top, a live monospaced log of everything the plugin does (and every gui_* "
                   "call the host makes) below. Embedded windows only.",
    .features = kFeatures,
};

const clap_plugin* GuiPlugin::create(const clap_host* host) {
    return (new GuiPlugin(host))->clapPlugin();
}

GuiPlugin::GuiPlugin(const clap_host* host) : Plugin(&descriptor, host) {
    provideExtension(CLAP_EXT_AUDIO_PORTS, ext::audioPortsVtable(),
                     static_cast<ext::AudioPortsProvider*>(this));
    provideExtension(CLAP_EXT_PARAMS, ext::paramsVtable(), static_cast<ext::ParamsProvider*>(this));
    provideExtension(CLAP_EXT_STATE, ext::stateVtable(), static_cast<ext::StateProvider*>(this));
    provideExtension(CLAP_EXT_GUI, ext::guiVtable(), static_cast<ext::GuiProvider*>(this));
    setLifecycleLogging(true);
}

bool GuiPlugin::init() noexcept {
    if (host())
        _hostParams =
            static_cast<const clap_host_params*>(host()->get_extension(host(), CLAP_EXT_PARAMS));
    return true;
}

void GuiPlugin::deactivate() noexcept {
    // Drop stale GUI gestures so they don't leak into the next activation.
    _paramQueue.clear();
}

// ---- audio-ports ----

uint32_t GuiPlugin::audioPortCount(bool) noexcept {
    return 1;
}

bool GuiPlugin::audioPortInfo(uint32_t index, bool isInput,
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

std::atomic<double>* GuiPlugin::paramStorage(clap_id paramId) noexcept {
    switch (paramId) {
    case kParamGain:
        return &_gainDb;
    case kParamMode:
        return &_mode;
    case kParamMute:
        return &_mute;
    default:
        return nullptr;
    }
}

uint32_t GuiPlugin::paramCount() noexcept {
    return kParamCount;
}

bool GuiPlugin::paramInfo(uint32_t index, clap_param_info* info) noexcept {
    if (index >= kParamCount)
        return false;
    std::memset(info, 0, sizeof(*info));
    switch (index) {
    case 0:
        info->id = kParamGain;
        std::snprintf(info->name, sizeof(info->name), "Gain");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = -36.0;
        info->max_value = 36.0;
        info->default_value = 0.0;
        break;
    case 1:
        info->id = kParamMode;
        std::snprintf(info->name, sizeof(info->name), "Mode");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        info->min_value = 0.0;
        info->max_value = 3.0;
        info->default_value = 0.0;
        break;
    case 2:
        info->id = kParamMute;
        std::snprintf(info->name, sizeof(info->name), "Mute");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        break;
    }
    return true;
}

bool GuiPlugin::paramValue(clap_id paramId, double* value) noexcept {
    const auto* storage = paramStorage(paramId);
    if (!storage)
        return false;
    *value = storage->load(std::memory_order_relaxed);
    return true;
}

bool GuiPlugin::paramValueToText(clap_id paramId, double value, char* out,
                                 uint32_t capacity) noexcept {
    switch (paramId) {
    case kParamGain:
        std::snprintf(out, capacity, "%.1f dB", value);
        return true;
    case kParamMode: {
        const int index = value < 0.0 ? 0 : (value > 3.0 ? 3 : static_cast<int>(value + 0.5));
        std::snprintf(out, capacity, "%s", kModeNames[index]);
        return true;
    }
    case kParamMute:
        std::snprintf(out, capacity, "%s", value >= 0.5 ? "On" : "Off");
        return true;
    default:
        return false;
    }
}

bool GuiPlugin::paramTextToValue(clap_id paramId, const char* text, double* value) noexcept {
    switch (paramId) {
    case kParamGain: {
        char* end = nullptr;
        const double parsed = std::strtod(text, &end);
        if (end == text)
            return false;
        *value = parsed;
        return true;
    }
    case kParamMode:
        for (int i = 0; i < 4; ++i) {
            if (std::strncmp(text, kModeNames[i], std::strlen(kModeNames[i])) == 0) {
                *value = i;
                return true;
            }
        }
        return false;
    case kParamMute:
        if (std::strncmp(text, "On", 2) == 0)
            *value = 1.0;
        else if (std::strncmp(text, "Off", 3) == 0)
            *value = 0.0;
        else
            return false;
        return true;
    default:
        return false;
    }
}

void GuiPlugin::applyParamEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
    if (auto* storage = paramStorage(event->param_id))
        storage->store(event->value, std::memory_order_relaxed);
}

void GuiPlugin::paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept {
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i)
        applyParamEvent(in->get(in, i));
    _paramQueue.drain(out, [this](clap_id id, double value) {
        if (auto* storage = paramStorage(id))
            storage->store(value, std::memory_order_relaxed);
    });
}

// ---- state ----

bool GuiPlugin::stateSave(const clap_ostream* stream) noexcept {
    if (!streamWrite(stream, kStateMagic) || !streamWrite(stream, kStateVersion) ||
        !streamWrite(stream, kParamCount))
        return false;
    for (clap_id id : {kParamGain, kParamMode, kParamMute}) {
        const double value = paramStorage(id)->load(std::memory_order_relaxed);
        if (!streamWrite(stream, static_cast<uint32_t>(id)) || !streamWrite(stream, value))
            return false;
    }
    return true;
}

bool GuiPlugin::stateLoad(const clap_istream* stream) noexcept {
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
        if (auto* storage = paramStorage(id))
            storage->store(value, std::memory_order_relaxed);
    }
    return true;
}

// ---- gui (GuiProvider) ----

bool GuiPlugin::guiIsApiSupported(const char* api, bool isFloating) noexcept {
    const bool supported = !isFloating && api && std::strcmp(api, kPlatformApi) == 0;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "gui: is_api_supported(api=%s, floating=%d) -> %s",
                  api ? api : "null", isFloating ? 1 : 0, supported ? "true" : "false");
    logToHost(CLAP_LOG_DEBUG, buf);
    return supported;
}

bool GuiPlugin::guiGetPreferredApi(const char** api, bool* isFloating) noexcept {
    *api = kPlatformApi; // must be the constant pointer, not a copy
    *isFloating = false;
    logToHost(CLAP_LOG_DEBUG, "gui: get_preferred_api()");
    return true;
}

bool GuiPlugin::guiCreate(const char* api, bool isFloating) noexcept {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "gui: create(api=%s, floating=%d)", api ? api : "null",
                  isFloating ? 1 : 0);
    logToHost(CLAP_LOG_INFO, buf);
    if (isFloating || !api || std::strcmp(api, kPlatformApi) != 0)
        return false;
    if (_view) {
        logToHost(CLAP_LOG_HOST_MISBEHAVING, "gui: create() called while a GUI already exists");
        return false;
    }
    _view = createNativeView(*this);
    if (_view)
        _view->setScale(_scale);
    return _view != nullptr;
}

void GuiPlugin::guiDestroy() noexcept {
    logToHost(CLAP_LOG_INFO, "gui: destroy()");
    if (!_view)
        logToHost(CLAP_LOG_HOST_MISBEHAVING, "gui: destroy() called without a GUI");
    _view.reset();
}

bool GuiPlugin::guiSetScale(double scale) noexcept {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "gui: set_scale(%.2f)", scale);
    logToHost(CLAP_LOG_DEBUG, buf);
#if defined(__APPLE__)
    // Cocoa uses logical sizes; per spec the host should not call set_scale.
    return false;
#else
    _scale = scale > 0.0 ? scale : 1.0;
    if (_view)
        _view->setScale(_scale);
    return true;
#endif
}

bool GuiPlugin::guiGetSize(uint32_t* width, uint32_t* height) noexcept {
    if (_view) {
        _view->getSize(width, height);
    } else {
        logToHost(CLAP_LOG_HOST_MISBEHAVING, "gui: get_size() called without create()");
        *width = static_cast<uint32_t>(NativeView::kDefaultWidth * _scale);
        *height = static_cast<uint32_t>(NativeView::minHeightFor(kParamCount) * _scale);
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "gui: get_size() -> %ux%u", *width, *height);
    logToHost(CLAP_LOG_DEBUG, buf);
    return true;
}

bool GuiPlugin::guiCanResize() noexcept {
    logToHost(CLAP_LOG_DEBUG, "gui: can_resize() -> true");
    return true;
}

bool GuiPlugin::guiGetResizeHints(clap_gui_resize_hints* hints) noexcept {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 0;
    hints->aspect_ratio_height = 0;
    logToHost(CLAP_LOG_DEBUG, "gui: get_resize_hints()");
    return true;
}

void GuiPlugin::adjustToMinimum(uint32_t* width, uint32_t* height) noexcept {
    const auto minWidth = static_cast<uint32_t>(NativeView::kMinWidth * _scale);
    const auto minHeight = static_cast<uint32_t>(NativeView::minHeightFor(kParamCount) * _scale);
    if (*width < minWidth)
        *width = minWidth;
    if (*height < minHeight)
        *height = minHeight;
}

bool GuiPlugin::guiAdjustSize(uint32_t* width, uint32_t* height) noexcept {
    const uint32_t requestedWidth = *width, requestedHeight = *height;
    adjustToMinimum(width, height);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "gui: adjust_size(%ux%u) -> %ux%u", requestedWidth,
                  requestedHeight, *width, *height);
    logToHost(CLAP_LOG_DEBUG, buf);
    return true;
}

bool GuiPlugin::guiSetSize(uint32_t width, uint32_t height) noexcept {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "gui: set_size(%ux%u)", width, height);
    logToHost(CLAP_LOG_DEBUG, buf);
    adjustToMinimum(&width, &height); // clamp-and-accept
    return _view ? _view->setSize(width, height) : false;
}

bool GuiPlugin::guiSetParent(const clap_window* window) noexcept {
    logToHost(CLAP_LOG_INFO, "gui: set_parent()");
    if (!_view) {
        logToHost(CLAP_LOG_HOST_MISBEHAVING, "gui: set_parent() called without create()");
        return false;
    }
    return _view->attach(window);
}

bool GuiPlugin::guiSetTransient(const clap_window*) noexcept {
    logToHost(CLAP_LOG_HOST_MISBEHAVING,
              "gui: set_transient() called on an embedded GUI (floating-only API)");
    return false;
}

void GuiPlugin::guiSuggestTitle(const char* title) noexcept {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "gui: suggest_title('%s') called on an embedded GUI (floating-only API)",
                  title ? title : "null");
    logToHost(CLAP_LOG_HOST_MISBEHAVING, buf);
}

bool GuiPlugin::guiShow() noexcept {
    logToHost(CLAP_LOG_INFO, "gui: show()");
    if (!_view)
        return false;
    _view->show();
    return true;
}

bool GuiPlugin::guiHide() noexcept {
    logToHost(CLAP_LOG_INFO, "gui: hide()");
    if (!_view)
        return false;
    _view->hide();
    return true;
}

// ---- GuiModel ----

uint32_t GuiPlugin::guiParamCount() noexcept {
    return kParamCount;
}

bool GuiPlugin::guiParamDesc(uint32_t index, ParamDesc* desc) noexcept {
    clap_param_info info;
    if (!paramInfo(index, &info))
        return false;
    desc->id = info.id;
    std::snprintf(desc->name, sizeof(desc->name), "%s", info.name);
    desc->minValue = info.min_value;
    desc->maxValue = info.max_value;
    desc->defaultValue = info.default_value;
    desc->stepped = (info.flags & CLAP_PARAM_IS_STEPPED) != 0;
    return true;
}

double GuiPlugin::guiParamValue(clap_id paramId) noexcept {
    const auto* storage = paramStorage(paramId);
    return storage ? storage->load(std::memory_order_relaxed) : 0.0;
}

void GuiPlugin::guiParamValueText(clap_id paramId, double value, char* out,
                                  uint32_t capacity) noexcept {
    if (!paramValueToText(paramId, value, out, capacity) && capacity > 0)
        out[0] = '\0';
}

void GuiPlugin::guiBeginGesture(clap_id paramId) noexcept {
    _paramQueue.push({ParamEventQueue::Kind::GestureBegin, paramId, 0.0});
    if (_hostParams && host())
        _hostParams->request_flush(host());
}

void GuiPlugin::guiSetValue(clap_id paramId, double value) noexcept {
    // Apply immediately so the UI doesn't snap back; the queued event is for
    // the host's automation recording (re-applying on drain is idempotent).
    if (auto* storage = paramStorage(paramId))
        storage->store(value, std::memory_order_relaxed);
    _paramQueue.push({ParamEventQueue::Kind::Value, paramId, value});
    if (_hostParams && host())
        _hostParams->request_flush(host());
}

void GuiPlugin::guiEndGesture(clap_id paramId) noexcept {
    _paramQueue.push({ParamEventQueue::Kind::GestureEnd, paramId, 0.0});
    if (_hostParams && host())
        _hostParams->request_flush(host());
}

LogBuffer& GuiPlugin::guiLog() noexcept {
    return logBuffer();
}

// ---- processing ----

clap_process_status GuiPlugin::process(const clap_process* process) noexcept {
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_ERROR;

    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            applyParamEvent(process->in_events->get(process->in_events, i));
    }
    _paramQueue.drain(process->out_events, [this](clap_id id, double value) {
        if (auto* storage = paramStorage(id))
            storage->store(value, std::memory_order_relaxed);
    });

    const bool mute = _mute.load(std::memory_order_relaxed) >= 0.5;
    const float gain =
        mute ? 0.0f
             : static_cast<float>(std::pow(10.0, _gainDb.load(std::memory_order_relaxed) / 20.0));

    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels =
        in.channel_count < out.channel_count ? in.channel_count : out.channel_count;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        const float* src = in.data32[ch];
        float* dst = out.data32[ch];
        for (uint32_t i = 0; i < frames; ++i)
            dst[i] = src[i] * gain;
    }
    return CLAP_PROCESS_CONTINUE;
}

} // namespace cvp
