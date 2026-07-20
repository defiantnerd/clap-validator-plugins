#include "wrapper/ext/gui.h"

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

GuiProvider* provider(const clap_plugin* p) {
    return Plugin::from(p)->extensionProvider<GuiProvider>(CLAP_EXT_GUI);
}

// gui.h call order: everything except is_api_supported/get_preferred_api/
// create requires a created gui (G01), and most methods are additionally
// restricted to embedded ([!floating]) or floating windows (G04). The
// created/floating state lives in the ContractMonitor so the checks are
// uniform across flavors.

bool requireCreated(const clap_plugin* p, const char* function) {
    auto& contract = Plugin::from(p)->contract();
    if (contract.guiCreated())
        return true;
    contract.report(Violation::G01, function, "called before gui create()");
    return false;
}

bool requireEmbedded(const clap_plugin* p, const char* function) { // [!floating]
    auto& contract = Plugin::from(p)->contract();
    if (!contract.guiFloating())
        return true;
    contract.report(Violation::G04, function, "embedded-only method called on a floating gui");
    return false;
}

bool requireFloating(const clap_plugin* p, const char* function) { // [floating]
    auto& contract = Plugin::from(p)->contract();
    if (contract.guiFloating())
        return true;
    contract.report(Violation::G04, function, "floating-only method called on an embedded gui");
    return false;
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
    auto& contract = Plugin::from(p)->contract();
    if (contract.guiCreated()) {
        contract.report(Violation::G02, "gui.create()", "called while a gui already exists");
        return false;
    }
    const bool ok = provider(p)->guiCreate(api, isFloating);
    if (ok)
        contract.setGuiCreated(true, isFloating);
    return ok;
}

void sDestroy(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    auto& contract = Plugin::from(p)->contract();
    if (!contract.guiCreated())
        contract.report(Violation::G03, "gui.destroy()", "called without a created gui");
    // Delegate regardless: the provider tears down defensively.
    provider(p)->guiDestroy();
    contract.setGuiCreated(false, false);
}

bool sSetScale(const clap_plugin* p, double scale) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.set_scale()"))
        return false;
    return provider(p)->guiSetScale(scale);
}

bool sGetSize(const clap_plugin* p, uint32_t* width, uint32_t* height) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.get_size()"))
        return false;
    return provider(p)->guiGetSize(width, height);
}

bool sCanResize(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.can_resize()") || !requireEmbedded(p, "gui.can_resize()"))
        return false;
    return provider(p)->guiCanResize();
}

bool sGetResizeHints(const clap_plugin* p, clap_gui_resize_hints* hints) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.get_resize_hints()") ||
        !requireEmbedded(p, "gui.get_resize_hints()"))
        return false;
    return provider(p)->guiGetResizeHints(hints);
}

bool sAdjustSize(const clap_plugin* p, uint32_t* width, uint32_t* height) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.adjust_size()") || !requireEmbedded(p, "gui.adjust_size()"))
        return false;
    return provider(p)->guiAdjustSize(width, height);
}

bool sSetSize(const clap_plugin* p, uint32_t width, uint32_t height) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.set_size()") || !requireEmbedded(p, "gui.set_size()"))
        return false;
    return provider(p)->guiSetSize(width, height);
}

bool sSetParent(const clap_plugin* p, const clap_window* window) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.set_parent()") || !requireEmbedded(p, "gui.set_parent()"))
        return false;
    return provider(p)->guiSetParent(window);
}

bool sSetTransient(const clap_plugin* p, const clap_window* window) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.set_transient()") || !requireFloating(p, "gui.set_transient()"))
        return false;
    return provider(p)->guiSetTransient(window);
}

void sSuggestTitle(const clap_plugin* p, const char* title) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.suggest_title()") || !requireFloating(p, "gui.suggest_title()"))
        return;
    provider(p)->guiSuggestTitle(title);
}

bool sShow(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.show()"))
        return false;
    return provider(p)->guiShow();
}

bool sHide(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    if (!requireCreated(p, "gui.hide()"))
        return false;
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
