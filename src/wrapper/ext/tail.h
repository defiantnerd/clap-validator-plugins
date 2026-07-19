#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.tail — implement and register with
// provideExtension(CLAP_EXT_TAIL, tailVtable(), static_cast<TailProvider*>(this)).
class TailProvider {
public:
    virtual ~TailProvider() = default;

    // Tail length in samples; UINT32_MAX means infinite.
    virtual uint32_t tail() noexcept = 0; // [main, audio]
};

const clap_plugin_tail* tailVtable() noexcept;

} // namespace cvp::ext
