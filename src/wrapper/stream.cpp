#include "wrapper/stream.h"

namespace cvp {

bool streamWriteAll(const clap_ostream* stream, const void* data, uint64_t size) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    while (size > 0) {
        const int64_t written = stream->write(stream, bytes, size);
        if (written <= 0)
            return false;
        bytes += written;
        size -= static_cast<uint64_t>(written);
    }
    return true;
}

bool streamReadAll(const clap_istream* stream, void* data, uint64_t size) noexcept {
    auto* bytes = static_cast<uint8_t*>(data);
    while (size > 0) {
        const int64_t read = stream->read(stream, bytes, size);
        if (read <= 0)
            return false;
        bytes += read;
        size -= static_cast<uint64_t>(read);
    }
    return true;
}

} // namespace cvp
