#pragma once

#include <atomic>
#include <memory>

#include "gui/gui_model.h"
#include "gui/native_view.h"
#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/gui.h"
#include "wrapper/ext/params.h"
#include "wrapper/ext/state.h"
#include "wrapper/param_queue.h"
#include "wrapper/plugin.h"

namespace cvp {

// Validator GUI — stereo gain effect with a framework-free native editor.
//
// The editor shows the parameters (editable sliders emitting proper CLAP
// gesture events) on top and a >=20-line monospaced log pane below. Every
// log line of the instance appears there (LogBuffer), and every gui_* call
// from the host is logged — the view live-documents the host's GUI protocol
// usage. Embedded windows only.
//
// Extension profile: audio-ports (stereo in/out), params, state, gui.
class GuiPlugin final : public Plugin,
                        public ext::AudioPortsProvider,
                        public ext::ParamsProvider,
                        public ext::StateProvider,
                        public ext::GuiProvider,
                        public GuiModel {
public:
    static const clap_plugin_descriptor descriptor;
    static const clap_plugin* create(const clap_host* host);

    explicit GuiPlugin(const clap_host* host);

protected:
    bool init() noexcept override;
    void deactivate() noexcept override;
    clap_process_status process(const clap_process* process) noexcept override;

    // ext::AudioPortsProvider
    uint32_t audioPortCount(bool isInput) noexcept override;
    bool audioPortInfo(uint32_t index, bool isInput, clap_audio_port_info* info) noexcept override;

    // ext::ParamsProvider
    uint32_t paramCount() noexcept override;
    bool paramInfo(uint32_t index, clap_param_info* info) noexcept override;
    bool paramValue(clap_id paramId, double* value) noexcept override;
    bool paramValueToText(clap_id paramId, double value, char* out,
                          uint32_t capacity) noexcept override;
    bool paramTextToValue(clap_id paramId, const char* text, double* value) noexcept override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    // ext::StateProvider
    bool stateSave(const clap_ostream* stream) noexcept override;
    bool stateLoad(const clap_istream* stream) noexcept override;

    // ext::GuiProvider
    bool guiIsApiSupported(const char* api, bool isFloating) noexcept override;
    bool guiGetPreferredApi(const char** api, bool* isFloating) noexcept override;
    bool guiCreate(const char* api, bool isFloating) noexcept override;
    void guiDestroy() noexcept override;
    bool guiSetScale(double scale) noexcept override;
    bool guiGetSize(uint32_t* width, uint32_t* height) noexcept override;
    bool guiCanResize() noexcept override;
    bool guiGetResizeHints(clap_gui_resize_hints* hints) noexcept override;
    bool guiAdjustSize(uint32_t* width, uint32_t* height) noexcept override;
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override;
    bool guiSetParent(const clap_window* window) noexcept override;
    bool guiSetTransient(const clap_window* window) noexcept override;
    void guiSuggestTitle(const char* title) noexcept override;
    bool guiShow() noexcept override;
    bool guiHide() noexcept override;

    // GuiModel
    uint32_t guiParamCount() noexcept override;
    bool guiParamDesc(uint32_t index, ParamDesc* desc) noexcept override;
    double guiParamValue(clap_id paramId) noexcept override;
    void guiParamValueText(clap_id paramId, double value, char* out,
                           uint32_t capacity) noexcept override;
    void guiBeginGesture(clap_id paramId) noexcept override;
    void guiSetValue(clap_id paramId, double value) noexcept override;
    void guiEndGesture(clap_id paramId) noexcept override;
    LogBuffer& guiLog() noexcept override;

private:
    enum ParamId : clap_id {
        kParamGain = 0, // dB
        kParamMode = 1, // stepped enum 0..3
        kParamMute = 2, // stepped bool
    };
    static constexpr uint32_t kParamCount = 3;

    void applyParamEvent(const clap_event_header* header) noexcept;
    std::atomic<double>* paramStorage(clap_id paramId) noexcept;
    void adjustToMinimum(uint32_t* width, uint32_t* height) noexcept;

    std::atomic<double> _gainDb{0.0};
    std::atomic<double> _mode{0.0};
    std::atomic<double> _mute{0.0};

    ParamEventQueue _paramQueue;
    const clap_host_params* _hostParams = nullptr;
    std::unique_ptr<NativeView> _view;
    double _scale = 1.0;
};

} // namespace cvp
