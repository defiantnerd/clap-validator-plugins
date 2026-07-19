#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.remote-controls — implement and register with BOTH ids (hosts may
// query the compat id):
//   provideExtension(CLAP_EXT_REMOTE_CONTROLS, remoteControlsVtable(),
//                    static_cast<RemoteControlsProvider*>(this));
//   provideExtension(CLAP_EXT_REMOTE_CONTROLS_COMPAT, remoteControlsVtable(),
//                    static_cast<RemoteControlsProvider*>(this));
class RemoteControlsProvider {
public:
    virtual ~RemoteControlsProvider() = default;

    virtual uint32_t remoteControlsPageCount() noexcept = 0; // [main]
    virtual bool remoteControlsPage(uint32_t pageIndex,
                                    clap_remote_controls_page* page) noexcept = 0; // [main]
};

const clap_plugin_remote_controls* remoteControlsVtable() noexcept;

// Fills a page struct; unused control slots are set to CLAP_INVALID_ID.
void fillRemoteControlsPage(clap_remote_controls_page* page, clap_id pageId,
                            const char* sectionName, const char* pageName,
                            const clap_id* paramIds, uint32_t paramCount) noexcept;

} // namespace cvp::ext
