#pragma once

#include <clap/clap.h>

#include <cstdint>

namespace cvp {

// Helpers over clap_istream/clap_ostream, which may read/write fewer bytes
// than requested per call — these loop until done or error.
bool streamWriteAll(const clap_ostream* stream, const void* data, uint64_t size) noexcept;
bool streamReadAll(const clap_istream* stream, void* data, uint64_t size) noexcept;

template <typename T>
bool streamWrite(const clap_ostream* stream, const T& value) noexcept {
    return streamWriteAll(stream, &value, sizeof(T));
}

template <typename T>
bool streamRead(const clap_istream* stream, T& value) noexcept {
    return streamReadAll(stream, &value, sizeof(T));
}

} // namespace cvp
