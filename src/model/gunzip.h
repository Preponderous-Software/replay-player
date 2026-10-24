// gunzip: undoes the gzip on usd-web's UWR1 chunks when the desktop player opens a bundle from
// disk. (In the browser the page uses DecompressionStream instead.) A plain RFC 1951 inflate
// with an RFC 1952 header and CRC-32 check; no dependencies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace rp {

class GunzipError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// True when `data` starts with the gzip magic bytes.
bool isGzip(const uint8_t* data, size_t size);

/// Decompresses one gzip member. Refuses output larger than `maxOutput` bytes. Throws GunzipError.
std::vector<uint8_t> gunzip(const uint8_t* data, size_t size, size_t maxOutput = size_t(1) << 30);

}  // namespace rp
