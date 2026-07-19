// Windows native view — raw Win32 (user32/gdi32/comctl32 system libraries
// only). Layout: one row per parameter (STATIC label | trackbar | STATIC
// value) on top, below a read-only multiline EDIT log pane (>=20 lines,
// Consolas) fed from the plugin's LogBuffer at ~10 Hz via WM_TIMER.
//
// NOTE: written on macOS without compile verification — see README.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commctrl.h>

#include <cstring>
#include <string>
#include <vector>

#include "gui/gui_model.h"
#include "gui/native_view.h"
#include "wrapper/logbuffer.h"

namespace cvp {

namespace {

constexpr wchar_t kClassName[] = L"CVPValidatorGuiView";
constexpr UINT_PTR kTimerId = 1;
constexpr int kTrackbarRange = 1000;
constexpr int kLabelWidth = 120;
constexpr int kValueWidth = 90;

std::wstring toWide(const char* utf8) {
    if (!utf8 || !*utf8)
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring wide(needed > 0 ? static_cast<size_t>(needed - 1) : 0, L'\0');
    if (needed > 1)
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), needed);
    return wide;
}

HINSTANCE moduleInstance() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleInstance), &module);
    return module;
}

class Win32NativeView final : public NativeView {
public:
    explicit Win32NativeView(GuiModel& model) : _model(model) {
        const uint32_t count = _model.guiParamCount();
        for (uint32_t i = 0; i < count; ++i) {
            GuiModel::ParamDesc desc{};
            if (_model.guiParamDesc(i, &desc)) {
                _params.push_back(desc);
                _inGesture.push_back(false);
            }
        }
        _widthPx = scaled(kDefaultWidth);
        _heightPx = scaled(minHeightFor(static_cast<uint32_t>(_params.size())));
    }

    ~Win32NativeView() override {
        if (_hwnd) {
            KillTimer(_hwnd, kTimerId);
            SetWindowLongPtrW(_hwnd, GWLP_USERDATA, 0);
            DestroyWindow(_hwnd);
        }
        if (_monoFont)
            DeleteObject(_monoFont);
        if (_uiFont)
            DeleteObject(_uiFont);
    }

    bool attach(const clap_window* parent) noexcept override {
        if (_hwnd || !parent || !parent->win32)
            return false;
        auto parentHwnd = static_cast<HWND>(parent->win32);

        if (!_scaleExplicit) {
            const UINT dpi = GetDpiForWindow(parentHwnd);
            if (dpi > 0)
                _scale = dpi / 96.0;
            _widthPx = scaled(kDefaultWidth);
            _heightPx = scaled(minHeightFor(static_cast<uint32_t>(_params.size())));
        }

        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES};
        InitCommonControlsEx(&icc);
        registerClassOnce();

        _hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0,
                                static_cast<int>(_widthPx), static_cast<int>(_heightPx),
                                parentHwnd, nullptr, moduleInstance(), this);
        if (!_hwnd)
            return false;

        createChildren();
        layoutChildren();
        SetTimer(_hwnd, kTimerId, 100, nullptr);
        return true;
    }

    void getSize(uint32_t* width, uint32_t* height) noexcept override {
        *width = _widthPx;
        *height = _heightPx;
    }

    bool setSize(uint32_t width, uint32_t height) noexcept override {
        _widthPx = width;
        _heightPx = height;
        if (_hwnd) {
            MoveWindow(_hwnd, 0, 0, static_cast<int>(width), static_cast<int>(height), TRUE);
            layoutChildren();
        }
        return true;
    }

    void setScale(double scale) noexcept override {
        if (scale <= 0.0)
            return;
        _scale = scale;
        _scaleExplicit = true;
        if (!_hwnd) {
            _widthPx = scaled(kDefaultWidth);
            _heightPx = scaled(minHeightFor(static_cast<uint32_t>(_params.size())));
        }
    }

    void show() noexcept override {
        if (_hwnd) {
            ShowWindow(_hwnd, SW_SHOW);
            SetTimer(_hwnd, kTimerId, 100, nullptr);
        }
    }

    void hide() noexcept override {
        if (_hwnd) {
            KillTimer(_hwnd, kTimerId); // stop painting while hidden
            ShowWindow(_hwnd, SW_HIDE);
        }
    }

private:
    uint32_t scaled(uint32_t logical) const noexcept {
        return static_cast<uint32_t>(logical * _scale + 0.5);
    }

    static void registerClassOnce() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Win32NativeView::wndProc;
        wc.hInstance = moduleInstance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            // Registration genuinely failed; CreateWindowExW will fail and
            // attach() reports false.
        }
    }

    void createChildren() {
        const HINSTANCE inst = moduleInstance();

        _uiFont = CreateFontW(-static_cast<int>(scaled(12)), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                              FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        _monoFont = CreateFontW(-static_cast<int>(scaled(12)), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        for (const auto& desc : _params) {
            HWND name = CreateWindowExW(0, L"STATIC", toWide(desc.name).c_str(),
                                        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, 0, 0, 10, 10,
                                        _hwnd, nullptr, inst, nullptr);
            SendMessageW(name, WM_SETFONT, reinterpret_cast<WPARAM>(_uiFont), TRUE);
            _nameLabels.push_back(name);

            HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                          WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0,
                                          10, 10, _hwnd, nullptr, inst, nullptr);
            const int range = trackbarRange(desc);
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, range));
            SendMessageW(slider, TBM_SETPOS, TRUE,
                         valueToPos(desc, _model.guiParamValue(desc.id)));
            _sliders.push_back(slider);

            HWND value = CreateWindowExW(0, L"STATIC", L"",
                                         WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_LEFTNOWORDWRAP, 0,
                                         0, 10, 10, _hwnd, nullptr, inst, nullptr);
            SendMessageW(value, WM_SETFONT, reinterpret_cast<WPARAM>(_monoFont), TRUE);
            _valueLabels.push_back(value);
        }

        _copyButton = CreateWindowExW(0, L"BUTTON", L"Copy Log",
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, _hwnd,
                                      nullptr, inst, nullptr);
        SendMessageW(_copyButton, WM_SETFONT, reinterpret_cast<WPARAM>(_uiFont), TRUE);

        _logEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                       ES_AUTOVSCROLL | ES_READONLY,
                                   0, 0, 10, 10, _hwnd, nullptr, inst, nullptr);
        SendMessageW(_logEdit, WM_SETFONT, reinterpret_cast<WPARAM>(_monoFont), TRUE);
    }

    void layoutChildren() {
        const int pad = static_cast<int>(scaled(kPadding));
        const int rowH = static_cast<int>(scaled(kRowHeight));
        const int labelW = static_cast<int>(scaled(kLabelWidth));
        const int valueW = static_cast<int>(scaled(kValueWidth));
        const int width = static_cast<int>(_widthPx);
        const int height = static_cast<int>(_heightPx);

        for (size_t i = 0; i < _params.size(); ++i) {
            const int y = pad + static_cast<int>(i) * rowH;
            MoveWindow(_nameLabels[i], pad, y + 4, labelW, rowH - 8, TRUE);
            MoveWindow(_sliders[i], pad + labelW + 6, y + 2,
                       width - labelW - valueW - 3 * pad - 12, rowH - 4, TRUE);
            MoveWindow(_valueLabels[i], width - valueW - pad, y + 4, valueW, rowH - 8, TRUE);
        }
        const int buttonTop =
            static_cast<int>(scaled(buttonRowTopFor(static_cast<uint32_t>(_params.size()))));
        const int buttonW = static_cast<int>(scaled(110));
        const int buttonH = static_cast<int>(scaled(kButtonRowHeight - 4));
        MoveWindow(_copyButton, width - pad - buttonW, buttonTop, buttonW, buttonH, TRUE);

        const int logTop =
            static_cast<int>(scaled(logTopFor(static_cast<uint32_t>(_params.size()))));
        MoveWindow(_logEdit, pad, logTop, width - 2 * pad, height - logTop - pad, TRUE);
    }

    int trackbarRange(const GuiModel::ParamDesc& desc) const {
        if (desc.stepped)
            return static_cast<int>(desc.maxValue - desc.minValue);
        return kTrackbarRange;
    }

    int valueToPos(const GuiModel::ParamDesc& desc, double value) const {
        if (desc.stepped)
            return static_cast<int>(value - desc.minValue + 0.5);
        const double range = desc.maxValue - desc.minValue;
        return range > 0.0
                   ? static_cast<int>((value - desc.minValue) / range * kTrackbarRange + 0.5)
                   : 0;
    }

    double posToValue(const GuiModel::ParamDesc& desc, int pos) const {
        if (desc.stepped)
            return desc.minValue + pos;
        return desc.minValue + (desc.maxValue - desc.minValue) * pos / kTrackbarRange;
    }

    void onHScroll(WPARAM wParam, LPARAM lParam) {
        const HWND slider = reinterpret_cast<HWND>(lParam);
        for (size_t i = 0; i < _sliders.size(); ++i) {
            if (_sliders[i] != slider)
                continue;
            const auto& desc = _params[i];
            const int code = LOWORD(wParam);
            // HIWORD(wParam) is only valid for thumb codes — always TBM_GETPOS.
            const int pos = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
            if (code == TB_ENDTRACK) {
                if (!_inGesture[i]) {
                    _model.guiBeginGesture(desc.id);
                    _model.guiSetValue(desc.id, posToValue(desc, pos));
                }
                _model.guiEndGesture(desc.id);
                _inGesture[i] = false;
            } else {
                if (!_inGesture[i]) {
                    _inGesture[i] = true;
                    _model.guiBeginGesture(desc.id);
                }
                _model.guiSetValue(desc.id, posToValue(desc, pos));
            }
            return;
        }
    }

    void copyLog() {
        const auto lines = _model.guiLog().snapshot();
        std::string joined;
        joined.reserve(lines.size() * 64);
        for (const auto& line : lines) {
            joined += line;
            joined += "\r\n";
        }
        const std::wstring wide = toWide(joined.c_str());
        if (!OpenClipboard(_hwnd))
            return;
        EmptyClipboard();
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (mem) {
            auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
            std::memcpy(dst, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem))
                GlobalFree(mem);
        }
        CloseClipboard();
        char msg[64];
        std::snprintf(msg, sizeof(msg), "gui: copied %zu log lines to clipboard", lines.size());
        _model.guiLog().append(CLAP_LOG_INFO, msg);
    }

    void tick() {
        char text[96];
        for (size_t i = 0; i < _params.size(); ++i) {
            const auto& desc = _params[i];
            const double value = _model.guiParamValue(desc.id);
            _model.guiParamValueText(desc.id, value, text, sizeof(text));
            SetWindowTextW(_valueLabels[i], toWide(text).c_str());
            if (!_inGesture[i])
                SendMessageW(_sliders[i], TBM_SETPOS, TRUE, valueToPos(desc, value));
        }

        const uint64_t version = _model.guiLog().version();
        if (version != _logVersion) {
            _logVersion = version;
            std::string joined;
            for (const auto& line : _model.guiLog().snapshot()) {
                joined += line;
                joined += "\r\n";
            }
            const std::wstring wide = toWide(joined.c_str());
            SetWindowTextW(_logEdit, wide.c_str());
            const auto length = static_cast<WPARAM>(wide.size());
            SendMessageW(_logEdit, EM_SETSEL, length, length);
            SendMessageW(_logEdit, EM_SCROLLCARET, 0, 0);
        }
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        auto* self =
            reinterpret_cast<Win32NativeView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg) {
        case WM_TIMER:
            if (wParam == kTimerId)
                self->tick();
            return 0;
        case WM_HSCROLL:
            if (lParam)
                self->onHScroll(wParam, lParam);
            return 0;
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lParam) == self->_copyButton &&
                HIWORD(wParam) == BN_CLICKED)
                self->copyLog();
            return 0;
        case WM_SIZE:
            self->_widthPx = LOWORD(lParam);
            self->_heightPx = HIWORD(lParam);
            self->layoutChildren();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerId);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    GuiModel& _model;
    std::vector<GuiModel::ParamDesc> _params;
    std::vector<bool> _inGesture;
    std::vector<HWND> _nameLabels;
    std::vector<HWND> _sliders;
    std::vector<HWND> _valueLabels;
    HWND _hwnd = nullptr;
    HWND _copyButton = nullptr;
    HWND _logEdit = nullptr;
    HFONT _uiFont = nullptr;
    HFONT _monoFont = nullptr;
    double _scale = 1.0;
    bool _scaleExplicit = false;
    uint32_t _widthPx = 0;
    uint32_t _heightPx = 0;
    uint64_t _logVersion = ~0ull;
};

} // namespace

std::unique_ptr<NativeView> createNativeView(GuiModel& model) {
    return std::make_unique<Win32NativeView>(model);
}

} // namespace cvp

#endif // _WIN32
