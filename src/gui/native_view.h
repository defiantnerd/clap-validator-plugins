#pragma once

#include <clap/clap.h>

#include <cstdint>
#include <memory>

namespace cvp {

class GuiModel;

// A platform-native, framework-free view: a transport section (two lines
// reflecting the host's clap_event_transport) at the very top, parameter rows
// (label + slider + value text) below it, a button row (Copy Log -> system
// clipboard), and a >=20-line monospaced log pane rendered from the plugin's
// LogBuffer, refreshed ~10x per second.
//
// Lifecycle: constructed unattached (getSize/setSize must already work —
// setSize before attach() records a pending size). attach() embeds into the
// host-provided parent window. Destruction is safe whether or not attach()
// was ever called. All methods are called from the CLAP main thread.
class NativeView {
public:
    virtual ~NativeView() = default;

    virtual bool attach(const clap_window* parent) noexcept = 0;
    virtual void getSize(uint32_t* width, uint32_t* height) noexcept = 0;
    virtual bool setSize(uint32_t width, uint32_t height) noexcept = 0;
    virtual void setScale(double scale) noexcept = 0;
    virtual void show() noexcept = 0;
    virtual void hide() noexcept = 0;

    // Logical layout constants shared by all platforms (scaled on win32/x11).
    static constexpr uint32_t kDefaultWidth = 560;
    static constexpr uint32_t kMinWidth = 420;
    static constexpr uint32_t kRowHeight = 26;
    static constexpr uint32_t kButtonRowHeight = 28;
    static constexpr uint32_t kTransportHeight = 38; // two text lines
    static constexpr uint32_t kLogLines = 20;
    static constexpr uint32_t kLogLineHeight = 15;
    static constexpr uint32_t kPadding = 8;

    static uint32_t paramsTopFor() noexcept { return kPadding + kTransportHeight + kPadding; }

    static uint32_t buttonRowTopFor(uint32_t paramCount) noexcept {
        return paramsTopFor() + paramCount * kRowHeight + kPadding;
    }

    static uint32_t logTopFor(uint32_t paramCount) noexcept {
        return buttonRowTopFor(paramCount) + kButtonRowHeight + kPadding;
    }

    static uint32_t minHeightFor(uint32_t paramCount) noexcept {
        return logTopFor(paramCount) + kLogLines * kLogLineHeight + kPadding;
    }
};

// Implemented once per platform (native_view_mac.mm / _win32.cpp / _x11.cpp).
std::unique_ptr<NativeView> createNativeView(GuiModel& model);

} // namespace cvp
