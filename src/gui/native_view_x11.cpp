// Linux native view — raw Xlib, no toolkit. A dedicated UI thread owns its
// own Display connection (every X call is confined to that thread, so
// XInitThreads() is not needed and must not be used). The CLAP main thread
// communicates via a mutex-guarded command queue woken through a self-pipe.
//
// Layout: one row per parameter (name | self-drawn slider | value text) on
// top, below a >=20-line log pane in the core monospaced font, redrawn when
// the LogBuffer version changes (checked on a ~100 ms tick).
//
// NOTE: written on macOS without compile verification — see README.

#if !defined(_WIN32) && !defined(__APPLE__)

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gui/gui_model.h"
#include "gui/native_view.h"
#include "gui/transport_format.h"
#include "wrapper/logbuffer.h"

namespace cvp {

namespace {

constexpr int kLabelWidth = 120;
constexpr int kValueWidth = 90;

class X11NativeView final : public NativeView {
    struct Command {
        enum class Kind { Resize, Show, Hide, Quit } kind;
        uint32_t width = 0;
        uint32_t height = 0;
    };

public:
    explicit X11NativeView(GuiModel& model) : _model(model) {
        const uint32_t count = _model.guiParamCount();
        for (uint32_t i = 0; i < count; ++i) {
            GuiModel::ParamDesc desc{};
            if (_model.guiParamDesc(i, &desc))
                _params.push_back(desc);
        }
        _widthPx = scaled(kDefaultWidth);
        _heightPx = scaled(minHeightFor(static_cast<uint32_t>(_params.size())));
        _pipe[0] = _pipe[1] = -1;
    }

    ~X11NativeView() override {
        if (_thread.joinable()) {
            pushCommand({Command::Kind::Quit});
            _thread.join();
        }
        if (_pipe[0] >= 0)
            close(_pipe[0]);
        if (_pipe[1] >= 0)
            close(_pipe[1]);
    }

    bool attach(const clap_window* parent) noexcept override {
        if (_thread.joinable() || !parent || !parent->x11)
            return false;
        if (pipe2(_pipe, O_CLOEXEC) != 0)
            return false;
        _parentXid = parent->x11;
        _thread = std::thread([this] { uiThread(); });
        return true;
    }

    void getSize(uint32_t* width, uint32_t* height) noexcept override {
        *width = _widthPx.load(std::memory_order_relaxed);
        *height = _heightPx.load(std::memory_order_relaxed);
    }

    bool setSize(uint32_t width, uint32_t height) noexcept override {
        _widthPx.store(width, std::memory_order_relaxed);
        _heightPx.store(height, std::memory_order_relaxed);
        if (_thread.joinable())
            pushCommand({Command::Kind::Resize, width, height});
        return true;
    }

    void setScale(double scale) noexcept override {
        if (scale <= 0.0)
            return;
        const bool resize = !_thread.joinable();
        _scale = scale;
        if (resize) {
            _widthPx.store(scaled(kDefaultWidth), std::memory_order_relaxed);
            _heightPx.store(scaled(minHeightFor(static_cast<uint32_t>(_params.size()))),
                            std::memory_order_relaxed);
        }
    }

    void show() noexcept override {
        if (_thread.joinable())
            pushCommand({Command::Kind::Show});
    }

    void hide() noexcept override {
        if (_thread.joinable())
            pushCommand({Command::Kind::Hide});
    }

private:
    uint32_t scaled(uint32_t logical) const noexcept {
        return static_cast<uint32_t>(logical * _scale + 0.5);
    }

    void pushCommand(const Command& command) {
        {
            std::lock_guard<std::mutex> lock(_commandMutex);
            _commands.push_back(command);
        }
        const char byte = 1;
        [[maybe_unused]] ssize_t written = write(_pipe[1], &byte, 1);
    }

    // ---------- everything below runs exclusively on the UI thread ----------

    void uiThread() {
        _display = XOpenDisplay(nullptr);
        if (!_display)
            return;
        const int screen = DefaultScreen(_display);
        const uint32_t width = _widthPx.load(std::memory_order_relaxed);
        const uint32_t height = _heightPx.load(std::memory_order_relaxed);

        _background = allocColor(0xEE, 0xEE, 0xEE);
        _trackColor = allocColor(0xC0, 0xC0, 0xC0);
        _fillColor = allocColor(0x4A, 0x78, 0xB0);
        _okColor = allocColor(0x20, 0x8C, 0x3C);
        _errorColor = allocColor(0xC8, 0x20, 0x20);
        _textColor = BlackPixel(_display, screen);

        _window = XCreateSimpleWindow(_display, static_cast<Window>(_parentXid), 0, 0, width,
                                      height, 0, _textColor, _background);

        _atomClipboard = XInternAtom(_display, "CLIPBOARD", False);
        _atomUtf8 = XInternAtom(_display, "UTF8_STRING", False);
        _atomTargets = XInternAtom(_display, "TARGETS", False);

        // _XEMBED_INFO: version 0, flags 1 (XEMBED_MAPPED) — some GtkSocket/
        // Qt-based hosts check for it; the full handshake is not needed for a
        // mouse-only UI.
        const Atom xembedInfo = XInternAtom(_display, "_XEMBED_INFO", False);
        const long info[2] = {0, 1};
        XChangeProperty(_display, _window, xembedInfo, xembedInfo, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(info), 2);

        XSelectInput(_display, _window,
                     ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                         StructureNotifyMask);

        loadFont();
        _gc = XCreateGC(_display, _window, 0, nullptr);
        XMapWindow(_display, _window);
        XFlush(_display);

        const int xfd = ConnectionNumber(_display);
        bool running = true;
        while (running) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(xfd, &readSet);
            FD_SET(_pipe[0], &readSet);
            timeval timeout{0, 100 * 1000}; // 100 ms tick
            select((xfd > _pipe[0] ? xfd : _pipe[0]) + 1, &readSet, nullptr, nullptr, &timeout);

            if (FD_ISSET(_pipe[0], &readSet)) {
                char drainBuf[16];
                while (read(_pipe[0], drainBuf, sizeof(drainBuf)) == sizeof(drainBuf)) {
                }
            }
            running = processCommands();
            if (!running)
                break;
            processXEvents();
            tick();
        }

        if (_font)
            XFreeFont(_display, _font);
        if (_gc)
            XFreeGC(_display, _gc);
        if (!_windowDead && _window)
            XDestroyWindow(_display, _window);
        XCloseDisplay(_display);
        _display = nullptr;
    }

    unsigned long allocColor(unsigned char r, unsigned char g, unsigned char b) {
        XColor color{};
        color.red = static_cast<unsigned short>(r << 8);
        color.green = static_cast<unsigned short>(g << 8);
        color.blue = static_cast<unsigned short>(b << 8);
        color.flags = DoRed | DoGreen | DoBlue;
        Colormap colormap = DefaultColormap(_display, DefaultScreen(_display));
        if (XAllocColor(_display, colormap, &color))
            return color.pixel;
        return WhitePixel(_display, DefaultScreen(_display));
    }

    void loadFont() {
        // Prefer a monospaced XLFD at roughly the scaled pixel size, fall
        // back to the always-present "fixed".
        char pattern[96];
        std::snprintf(pattern, sizeof(pattern), "-*-fixed-medium-r-*-*-%u-*-*-*-*-*-iso8859-1",
                      scaled(13));
        _font = XLoadQueryFont(_display, pattern);
        if (!_font)
            _font = XLoadQueryFont(_display, "fixed");
    }

    bool processCommands() {
        std::vector<Command> commands;
        {
            std::lock_guard<std::mutex> lock(_commandMutex);
            commands.swap(_commands);
        }
        for (const auto& command : commands) {
            switch (command.kind) {
            case Command::Kind::Quit:
                return false;
            case Command::Kind::Resize:
                if (!_windowDead) {
                    XResizeWindow(_display, _window, command.width, command.height);
                    _dirty = true;
                }
                break;
            case Command::Kind::Show:
                if (!_windowDead)
                    XMapWindow(_display, _window);
                break;
            case Command::Kind::Hide:
                if (!_windowDead)
                    XUnmapWindow(_display, _window);
                break;
            }
        }
        if (!_windowDead)
            XFlush(_display);
        return true;
    }

    void processXEvents() {
        while (!_windowDead && XPending(_display) > 0) {
            XEvent event;
            XNextEvent(_display, &event);
            switch (event.type) {
            case Expose:
                if (event.xexpose.count == 0)
                    _dirty = true;
                break;
            case ConfigureNotify:
                _widthPx.store(static_cast<uint32_t>(event.xconfigure.width),
                               std::memory_order_relaxed);
                _heightPx.store(static_cast<uint32_t>(event.xconfigure.height),
                                std::memory_order_relaxed);
                _dirty = true;
                break;
            case DestroyNotify:
                // Host destroyed the parent (and thus us) without gui->destroy.
                _windowDead = true;
                break;
            case ButtonPress:
                if (event.xbutton.button == Button1)
                    onMouseDown(event.xbutton.x, event.xbutton.y);
                break;
            case MotionNotify:
                if (_dragParam >= 0)
                    onMouseDrag(event.xmotion.x);
                break;
            case ButtonRelease:
                if (event.xbutton.button == Button1 && _dragParam >= 0) {
                    onMouseDrag(event.xbutton.x);
                    _model.guiEndGesture(_params[static_cast<size_t>(_dragParam)].id);
                    _dragParam = -1;
                }
                break;
            case SelectionRequest:
                onSelectionRequest(event.xselectionrequest);
                break;
            case SelectionClear:
                // Another client took the clipboard; nothing to release.
                break;
            default:
                break;
            }
        }
    }

    // Slider track geometry for row i.
    void trackRect(size_t i, int* x, int* y, int* w, int* h) const {
        const int pad = static_cast<int>(scaled(kPadding));
        const int rowH = static_cast<int>(scaled(kRowHeight));
        const int labelW = static_cast<int>(scaled(kLabelWidth));
        const int valueW = static_cast<int>(scaled(kValueWidth));
        const int width = static_cast<int>(_widthPx.load(std::memory_order_relaxed));
        *x = pad + labelW + 6;
        *y = static_cast<int>(scaled(paramsTopFor())) + static_cast<int>(i) * rowH + rowH / 2 - 4;
        *w = width - labelW - valueW - 3 * pad - 12;
        *h = 8;
    }

    void buttonRect(int* x, int* y, int* w, int* h) const {
        const int pad = static_cast<int>(scaled(kPadding));
        const int width = static_cast<int>(_widthPx.load(std::memory_order_relaxed));
        *w = static_cast<int>(scaled(110));
        *h = static_cast<int>(scaled(kButtonRowHeight - 6));
        *x = width - pad - *w;
        *y = static_cast<int>(scaled(buttonRowTopFor(static_cast<uint32_t>(_params.size()))));
    }

    // Copies the whole log to the X11 CLIPBOARD selection. The content is
    // served from this window for as long as it owns the selection (standard
    // X11 semantics; a clipboard manager may take a persistent copy).
    void copyLog() {
        const auto lines = _model.guiLog().snapshot();
        std::string joined;
        joined.reserve(lines.size() * 64);
        for (const auto& line : lines) {
            joined += line;
            joined += '\n';
        }
        _clipboardText = std::move(joined);
        XSetSelectionOwner(_display, _atomClipboard, _window, CurrentTime);
        char msg[64];
        std::snprintf(msg, sizeof(msg), "gui: copied %zu log lines to clipboard", lines.size());
        _model.guiLog().append(CLAP_LOG_INFO, msg);
        _dirty = true;
    }

    void onSelectionRequest(const XSelectionRequestEvent& request) {
        XSelectionEvent reply{};
        reply.type = SelectionNotify;
        reply.display = request.display;
        reply.requestor = request.requestor;
        reply.selection = request.selection;
        reply.target = request.target;
        reply.time = request.time;
        reply.property = None;

        // Obsolete clients pass property None: use the target as property.
        const Atom property = request.property != None ? request.property : request.target;

        if (request.selection == _atomClipboard && !_clipboardText.empty()) {
            if (request.target == _atomTargets) {
                const Atom targets[] = {_atomTargets, _atomUtf8, XA_STRING};
                XChangeProperty(_display, request.requestor, property, XA_ATOM, 32,
                                PropModeReplace,
                                reinterpret_cast<const unsigned char*>(targets), 3);
                reply.property = property;
            } else if (request.target == _atomUtf8 || request.target == XA_STRING) {
                XChangeProperty(_display, request.requestor, property, request.target, 8,
                                PropModeReplace,
                                reinterpret_cast<const unsigned char*>(_clipboardText.c_str()),
                                static_cast<int>(_clipboardText.size()));
                reply.property = property;
            }
        }
        XSendEvent(_display, request.requestor, False, 0, reinterpret_cast<XEvent*>(&reply));
        XFlush(_display);
    }

    void onMouseDown(int mouseX, int mouseY) {
        {
            int x, y, w, h;
            buttonRect(&x, &y, &w, &h);
            if (mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h) {
                copyLog();
                return;
            }
        }
        for (size_t i = 0; i < _params.size(); ++i) {
            int x, y, w, h;
            trackRect(i, &x, &y, &w, &h);
            if (mouseX >= x && mouseX <= x + w && mouseY >= y - 6 && mouseY <= y + h + 6) {
                _dragParam = static_cast<int>(i);
                _model.guiBeginGesture(_params[i].id);
                onMouseDrag(mouseX);
                return;
            }
        }
    }

    void onMouseDrag(int mouseX) {
        const auto index = static_cast<size_t>(_dragParam);
        const auto& desc = _params[index];
        int x, y, w, h;
        trackRect(index, &x, &y, &w, &h);
        double fraction = w > 0 ? static_cast<double>(mouseX - x) / w : 0.0;
        fraction = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
        double value = desc.minValue + (desc.maxValue - desc.minValue) * fraction;
        if (desc.stepped)
            value = desc.minValue +
                    static_cast<int>((value - desc.minValue) + 0.5); // snap to steps
        _model.guiSetValue(desc.id, value);
        _dirty = true;
    }

    void tick() {
        if (_windowDead)
            return;
        TransportLines transport = formatTransport(_model.guiTransport());
        if (transport.line1 != _transportLines.line1 ||
            transport.line2 != _transportLines.line2) {
            _transportLines = std::move(transport);
            _dirty = true;
        }
        const uint64_t version = _model.guiLog().version();
        if (version != _logVersion) {
            _logVersion = version;
            _dirty = true;
        }
        const uint32_t violations = _model.guiViolationTotal();
        if (violations != _lastViolations) {
            _lastViolations = violations;
            _dirty = true;
        }
        // Param values may change from host automation — redraw when moved.
        for (size_t i = 0; i < _params.size(); ++i) {
            const double value = _model.guiParamValue(_params[i].id);
            if (i >= _lastValues.size() || value != _lastValues[i]) {
                _dirty = true;
                break;
            }
        }
        if (_dirty) {
            _dirty = false;
            redraw();
        }
    }

    void redraw() {
        const uint32_t width = _widthPx.load(std::memory_order_relaxed);
        const uint32_t height = _heightPx.load(std::memory_order_relaxed);
        Pixmap buffer = XCreatePixmap(_display, _window, width, height,
                                      static_cast<unsigned>(DefaultDepth(
                                          _display, DefaultScreen(_display))));

        XSetForeground(_display, _gc, _background);
        XFillRectangle(_display, buffer, _gc, 0, 0, width, height);
        if (_font)
            XSetFont(_display, _gc, _font->fid);

        const int pad = static_cast<int>(scaled(kPadding));
        const int rowH = static_cast<int>(scaled(kRowHeight));
        const int fontAscent = _font ? _font->ascent : 10;
        const int lineH = _font ? _font->ascent + _font->descent + 2 : 15;

        // Transport section (two lines at the very top).
        XSetForeground(_display, _gc, _textColor);
        const int transportLineH = _font ? _font->ascent + _font->descent + 3 : 17;
        XDrawString(_display, buffer, _gc, pad, pad + fontAscent, _transportLines.line1.c_str(),
                    static_cast<int>(_transportLines.line1.size()));
        XDrawString(_display, buffer, _gc, pad, pad + transportLineH + fontAscent,
                    _transportLines.line2.c_str(),
                    static_cast<int>(_transportLines.line2.size()));

        const int paramsTop = static_cast<int>(scaled(paramsTopFor()));
        _lastValues.resize(_params.size());
        char text[96];
        for (size_t i = 0; i < _params.size(); ++i) {
            const auto& desc = _params[i];
            const double value = _model.guiParamValue(desc.id);
            _lastValues[i] = value;
            const int rowY = paramsTop + static_cast<int>(i) * rowH;

            XSetForeground(_display, _gc, _textColor);
            XDrawString(_display, buffer, _gc, pad, rowY + rowH / 2 + fontAscent / 2, desc.name,
                        static_cast<int>(std::strlen(desc.name)));

            int x, y, w, h;
            trackRect(i, &x, &y, &w, &h);
            XSetForeground(_display, _gc, _trackColor);
            XFillRectangle(_display, buffer, _gc, x, y, static_cast<unsigned>(w),
                           static_cast<unsigned>(h));
            const double range = desc.maxValue - desc.minValue;
            const double fraction = range > 0.0 ? (value - desc.minValue) / range : 0.0;
            XSetForeground(_display, _gc, _fillColor);
            XFillRectangle(_display, buffer, _gc, x, y,
                           static_cast<unsigned>(w * fraction), static_cast<unsigned>(h));

            _model.guiParamValueText(desc.id, value, text, sizeof(text));
            XSetForeground(_display, _gc, _textColor);
            const int textWidth =
                _font ? XTextWidth(_font, text, static_cast<int>(std::strlen(text))) : 0;
            XDrawString(_display, buffer, _gc, static_cast<int>(width) - pad - textWidth,
                        rowY + rowH / 2 + fontAscent / 2, text,
                        static_cast<int>(std::strlen(text)));
        }

        // Copy Log button.
        {
            int x, y, w, h;
            buttonRect(&x, &y, &w, &h);
            XSetForeground(_display, _gc, _trackColor);
            XFillRectangle(_display, buffer, _gc, x, y, static_cast<unsigned>(w),
                           static_cast<unsigned>(h));
            XSetForeground(_display, _gc, _textColor);
            XDrawRectangle(_display, buffer, _gc, x, y, static_cast<unsigned>(w),
                           static_cast<unsigned>(h));
            const char* label = "Copy Log";
            const int labelWidth =
                _font ? XTextWidth(_font, label, static_cast<int>(std::strlen(label))) : 48;
            XDrawString(_display, buffer, _gc, x + (w - labelWidth) / 2,
                        y + h / 2 + fontAscent / 2, label,
                        static_cast<int>(std::strlen(label)));
        }

        // Host-contract badge, left-aligned in the button row.
        {
            int x, y, w, h;
            buttonRect(&x, &y, &w, &h);
            char badge[64] = "contract: OK";
            const uint32_t violations = _model.guiViolationTotal();
            if (violations > 0) {
                char code[8] = "";
                _model.guiLastViolation(code, sizeof(code));
                std::snprintf(badge, sizeof(badge), "contract: %u violation%s [last %s]",
                              violations, violations == 1 ? "" : "s", code);
            }
            XSetForeground(_display, _gc, violations > 0 ? _errorColor : _okColor);
            XDrawString(_display, buffer, _gc, pad, y + h / 2 + fontAscent / 2, badge,
                        static_cast<int>(std::strlen(badge)));
        }

        // Log pane: white background, last lines that fit, bottom-anchored.
        const int logTop =
            static_cast<int>(scaled(logTopFor(static_cast<uint32_t>(_params.size()))));
        const int logHeight = static_cast<int>(height) - logTop - pad;
        XSetForeground(_display, _gc, WhitePixel(_display, DefaultScreen(_display)));
        XFillRectangle(_display, buffer, _gc, pad, logTop, width - 2 * pad,
                       static_cast<unsigned>(logHeight));
        XSetForeground(_display, _gc, _textColor);

        const auto lines = _model.guiLog().snapshot();
        const int visibleLines = logHeight / lineH;
        const size_t first =
            lines.size() > static_cast<size_t>(visibleLines) ? lines.size() - visibleLines : 0;
        int textY = logTop + fontAscent + 2;
        for (size_t i = first; i < lines.size(); ++i) {
            XDrawString(_display, buffer, _gc, pad + 4, textY, lines[i].c_str(),
                        static_cast<int>(lines[i].size()));
            textY += lineH;
        }

        XCopyArea(_display, buffer, _window, _gc, 0, 0, width, height, 0, 0);
        XFreePixmap(_display, buffer);
        XFlush(_display);
    }

    GuiModel& _model;
    std::vector<GuiModel::ParamDesc> _params;
    std::vector<double> _lastValues;

    // Shared with the CLAP main thread:
    std::atomic<uint32_t> _widthPx{0};
    std::atomic<uint32_t> _heightPx{0};
    std::mutex _commandMutex;
    std::vector<Command> _commands;
    int _pipe[2];
    unsigned long _parentXid = 0;
    double _scale = 1.0;
    std::thread _thread;

    // UI-thread only:
    Display* _display = nullptr;
    Window _window = 0;
    GC _gc = nullptr;
    XFontStruct* _font = nullptr;
    Atom _atomClipboard = None, _atomUtf8 = None, _atomTargets = None;
    std::string _clipboardText;
    unsigned long _background = 0, _trackColor = 0, _fillColor = 0, _textColor = 0;
    unsigned long _okColor = 0, _errorColor = 0;
    uint32_t _lastViolations = ~0u; // force first badge render
    bool _windowDead = false;
    bool _dirty = true;
    int _dragParam = -1;
    uint64_t _logVersion = ~0ull;
    TransportLines _transportLines;
};

} // namespace

std::unique_ptr<NativeView> createNativeView(GuiModel& model) {
    return std::make_unique<X11NativeView>(model);
}

} // namespace cvp

#endif // !_WIN32 && !__APPLE__
