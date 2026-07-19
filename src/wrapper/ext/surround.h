#pragma once

#include <clap/clap.h>

namespace cvp::ext {

// clap.surround — implement and register with BOTH ids (hosts may query the
// compat id):
//   provideExtension(CLAP_EXT_SURROUND, surroundVtable(),
//                    static_cast<SurroundProvider*>(this));
//   provideExtension(CLAP_EXT_SURROUND_COMPAT, surroundVtable(),
//                    static_cast<SurroundProvider*>(this));
class SurroundProvider {
public:
    virtual ~SurroundProvider() = default;

    virtual bool surroundIsChannelMaskSupported(uint64_t channelMask) noexcept = 0; // [main]
    // Returns the number of entries written (0 if the capacity is too small).
    virtual uint32_t surroundChannelMap(bool isInput, uint32_t portIndex, uint8_t* channelMap,
                                        uint32_t capacity) noexcept = 0; // [main]
};

const clap_plugin_surround* surroundVtable() noexcept;

} // namespace cvp::ext
