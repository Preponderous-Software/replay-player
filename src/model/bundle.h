// The usd-web replay bundle: `replay.json` (format "usd-web-replay", version 1) plus gzip'd,
// content-addressed UWR1 chunks of consecutive samples. The layout is usd-web's (ARCHITECTURE.md
// § Replays there); this is a second decoder for it, checked against usd-web's own Python
// decoder in CI (tests/cross_decode.py).
//
// Everything here treats the bundle as untrusted: every count is checked against the bytes that
// remain before anything is allocated, and a chunk that does not match its index is refused.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "model/model.h"

namespace rp {

constexpr const char* kBundleFormat = "usd-web-replay";
constexpr int kBundleVersion = 1;

class BundleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ChunkInfo {
    std::string file;
    int first = 0;
    int count = 0;
    uint64_t bytes = 0;  // gzip'd size, as listed
};

/// What decoding needs from replay.json besides the model itself.
struct BundleIndex {
    std::vector<ChunkInfo> chunks;
    double qmin[3] = {0, 0, 0};
    double qstep[3] = {0, 0, 0};
    int idBytes = 2;
    std::vector<double> widthMax;  // per set (vertex widths are u8 / 255 * widthMax)

    /// The chunk that holds sample k.
    int chunkOf(int k) const;
};

/// One chunk decoded but not yet dequantized: exactly what UWR1 carries (and what usd-web's
/// decodeChunk returns), so two decoders can be compared value for value.
struct DecodedSet {
    std::vector<uint32_t> ids;
    std::vector<uint16_t> q;      // 3 per entity
    std::vector<uint8_t> colors;  // 3 per entity, only with vertex colours
    std::vector<uint8_t> widths;  // 1 per entity, only with vertex widths
};
struct DecodedSample {
    std::vector<DecodedSet> sets;
    std::vector<std::optional<std::vector<uint8_t>>> fields;  // sRGB bytes, cols * rows * 3, or absent
};
struct DecodedChunk {
    uint32_t first = 0;
    uint32_t count = 0;
    std::vector<DecodedSample> samples;
};

/// Parses replay.json into a model with no samples loaded yet, plus the index. Refuses another
/// format, a newer version, and inconsistent sizes. Throws BundleError.
ReplayModel openBundle(const std::string& replayJson, const std::string& source, BundleIndex& index);

/// Decodes one chunk's bytes (gzip'd or already gunzipped) against the model's sets and fields.
DecodedChunk decodeChunk(const uint8_t* data, size_t size, const ReplayModel& model, const BundleIndex& index);

/// Stores a decoded chunk in the model (dequantized positions, colours and widths, field frames)
/// and marks its samples loaded. `chunk` is its position in index.chunks; a chunk whose first
/// sample or count differs from the index is refused.
void applyChunk(ReplayModel& model, const BundleIndex& index, int chunk, const DecodedChunk& decoded);

/// The decoded chunk as canonical JSON, the format tests/cross_decode.py prints from usd-web's
/// Python decoder: positions as min + q * step and widths as w / 255 * widthMax, in double, with
/// "%.17g".
std::string decodedToJson(const DecodedChunk& decoded, const ReplayModel& model, const BundleIndex& index);

/// Reads a bundle directory from disk (replay.json and every chunk). Throws BundleError.
ReplayModel loadBundleDir(const std::string& dir, BundleIndex* indexOut = nullptr);

}  // namespace rp
