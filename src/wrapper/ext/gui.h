#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.gui — implement and register with
// provideExtension(CLAP_EXT_GUI, guiVtable(), static_cast<GuiProvider*>(this)).
// All methods are [main-thread].
class GuiProvider {
public:
    virtual ~GuiProvider() = default;

    virtual bool guiIsApiSupported(const char* api, bool isFloating) noexcept = 0;
    virtual bool guiGetPreferredApi(const char** api, bool* isFloating) noexcept = 0;
    virtual bool guiCreate(const char* api, bool isFloating) noexcept = 0;
    virtual void guiDestroy() noexcept = 0;
    virtual bool guiSetScale(double scale) noexcept = 0;
    virtual bool guiGetSize(uint32_t* width, uint32_t* height) noexcept = 0;
    virtual bool guiCanResize() noexcept = 0;
    virtual bool guiGetResizeHints(clap_gui_resize_hints* hints) noexcept = 0;
    virtual bool guiAdjustSize(uint32_t* width, uint32_t* height) noexcept = 0;
    virtual bool guiSetSize(uint32_t width, uint32_t height) noexcept = 0;
    virtual bool guiSetParent(const clap_window* window) noexcept = 0;
    virtual bool guiSetTransient(const clap_window* window) noexcept = 0;
    virtual void guiSuggestTitle(const char* title) noexcept = 0;
    virtual bool guiShow() noexcept = 0;
    virtual bool guiHide() noexcept = 0;
};

const clap_plugin_gui* guiVtable() noexcept;

} // namespace cvp::ext
