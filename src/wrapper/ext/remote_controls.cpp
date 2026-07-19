#include "wrapper/ext/remote_controls.h"

#include <cstdio>
#include <cstring>

#include "wrapper/plugin.h"

namespace cvp::ext {

namespace {

RemoteControlsProvider* provider(const clap_plugin* p) {
    auto* iface =
        Plugin::from(p)->extensionProvider<RemoteControlsProvider>(CLAP_EXT_REMOTE_CONTROLS);
    if (!iface)
        iface = Plugin::from(p)->extensionProvider<RemoteControlsProvider>(
            CLAP_EXT_REMOTE_CONTROLS_COMPAT);
    return iface;
}

uint32_t sCount(const clap_plugin* p) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->remoteControlsPageCount();
}

bool sGet(const clap_plugin* p, uint32_t pageIndex, clap_remote_controls_page* page) {
    CVP_ASSERT_MAIN_THREAD(Plugin::from(p));
    return provider(p)->remoteControlsPage(pageIndex, page);
}

const clap_plugin_remote_controls kVtable = {
    .count = sCount,
    .get = sGet,
};

} // namespace

const clap_plugin_remote_controls* remoteControlsVtable() noexcept {
    return &kVtable;
}

void fillRemoteControlsPage(clap_remote_controls_page* page, clap_id pageId,
                            const char* sectionName, const char* pageName,
                            const clap_id* paramIds, uint32_t paramCount) noexcept {
    std::memset(page, 0, sizeof(*page));
    page->page_id = pageId;
    page->is_for_preset = false;
    std::snprintf(page->section_name, sizeof(page->section_name), "%s", sectionName);
    std::snprintf(page->page_name, sizeof(page->page_name), "%s", pageName);
    for (uint32_t i = 0; i < CLAP_REMOTE_CONTROLS_COUNT; ++i)
        page->param_ids[i] = i < paramCount ? paramIds[i] : CLAP_INVALID_ID;
}

} // namespace cvp::ext
