#include "wrapper/ext/gui.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

GuiProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<GuiProvider>(CLAP_EXT_GUI);
}

bool sIsApiSupported(const clap_plugin* p, const char* api, bool isFloating) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiIsApiSupported(api, isFloating);
}

bool sGetPreferredApi(const clap_plugin* p, const char** api, bool* isFloating) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiGetPreferredApi(api, isFloating);
}

bool sCreate(const clap_plugin* p, const char* api, bool isFloating) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiCreate(api, isFloating);
}

void sDestroy(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    provider(p)->guiDestroy();
}

bool sSetScale(const clap_plugin* p, double scale) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiSetScale(scale);
}

bool sGetSize(const clap_plugin* p, uint32_t* width, uint32_t* height) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiGetSize(width, height);
}

bool sCanResize(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiCanResize();
}

bool sGetResizeHints(const clap_plugin* p, clap_gui_resize_hints* hints) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiGetResizeHints(hints);
}

bool sAdjustSize(const clap_plugin* p, uint32_t* width, uint32_t* height) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiAdjustSize(width, height);
}

bool sSetSize(const clap_plugin* p, uint32_t width, uint32_t height) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiSetSize(width, height);
}

bool sSetParent(const clap_plugin* p, const clap_window* window) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiSetParent(window);
}

bool sSetTransient(const clap_plugin* p, const clap_window* window) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiSetTransient(window);
}

void sSuggestTitle(const clap_plugin* p, const char* title) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    provider(p)->guiSuggestTitle(title);
}

bool sShow(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiShow();
}

bool sHide(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->guiHide();
}

const clap_plugin_gui kVtable = {
    .is_api_supported = sIsApiSupported,
    .get_preferred_api = sGetPreferredApi,
    .create = sCreate,
    .destroy = sDestroy,
    .set_scale = sSetScale,
    .get_size = sGetSize,
    .can_resize = sCanResize,
    .get_resize_hints = sGetResizeHints,
    .adjust_size = sAdjustSize,
    .set_size = sSetSize,
    .set_parent = sSetParent,
    .set_transient = sSetTransient,
    .suggest_title = sSuggestTitle,
    .show = sShow,
    .hide = sHide,
};

} // namespace

const clap_plugin_gui* guiVtable() noexcept {
    return &kVtable;
}

} // namespace cvp::ext
