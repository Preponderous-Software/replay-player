// ReplayModel: one usd-replay/1 replay in memory, whichever way it arrived (a .usda/.usdc file
// through usd-recorder's C++ reader, or a usd-web bundle through the UWR1 decoder). It mirrors
// usd-recorder's Python `Replay` with the bundle's two additions: samples may be thinned
// (`keptEvery`), and a bundle's chunks arrive over time, so the model records which samples are
// loaded.
//
// Coordinates stay in the replay's own Z-up space; the renderer maps (x, y, z) to (x, z, -y).
// Colours are display (sRGB) values in 0..1, the space Strata writes to the screen (see
// ARCHITECTURE.md § Colour): bundle bytes are divided by 255, and a .usd file's linear colours
// are encoded to sRGB once, when the replay is loaded.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rp {

using Vec3 = std::array<float, 3>;

enum class Shape { Sphere, Cube };

struct SetDef {
    std::string id;
    std::string label;
    Shape shape = Shape::Sphere;
    bool vertexColor = false;
    bool vertexWidth = false;
    Vec3 color{0.6f, 0.65f, 0.7f};  // constant colour (when !vertexColor)
    float width = 1.0f;             // constant width (when !vertexWidth)
    int maxCount = 0;               // most entities in any sample
    uint32_t idCount = 0;           // ids are dense: 0 .. idCount-1, in first-seen order
};

struct FieldDef {
    std::string id;
    std::string label;
    int cols = 0;
    int rows = 0;
    double origin[2] = {0, 0};
    double cellSize = 1.0;
    double z = 0.0;
};

struct ScenePoints {
    std::string id;
    std::string label;
    Shape shape = Shape::Sphere;
    std::vector<Vec3> points;
    std::vector<Vec3> colors;   // one (constant) or one per point
    std::vector<float> widths;  // one (constant) or one per point
};

/// One set at one sample.
struct SetSample {
    std::vector<uint32_t> ids;  // dense ids, see SetDef::idCount
    std::vector<Vec3> points;
    std::vector<Vec3> colors;   // empty unless the set has vertex colours
    std::vector<float> widths;  // empty unless the set has vertex widths
};

struct Sample {
    std::vector<SetSample> sets;
};

/// The colours a field was sampled with, as RGBA8 texels ready for a texture (row j = cells
/// j * cols .. j * cols + cols - 1, from the grid's minimum corner).
struct FieldFrame {
    int sample = 0;  // the sample index it was sampled at (held until the next frame)
    std::vector<uint8_t> rgba;
};

struct Stat {
    std::string name;
    std::vector<std::optional<double>> values;  // one per sample, already held; empty before the first
};

struct Bounds {
    Vec3 min{0, 0, 0};
    Vec3 max{1, 1, 1};
};

/// An optional camera hint from the bundle's `view` (replay coordinates).
struct ViewHint {
    std::optional<Vec3> target;
    std::optional<double> distance;
    std::optional<double> theta;
    std::optional<double> phi;
};

struct ReplayModel {
    std::string source;  // what was opened (a path or a URL)
    std::string title;
    std::vector<std::pair<std::string, std::string>> meta;  // usdReplay strings and numbers, as text
    double playbackRate = 30.0;                             // ticks per second at 1x
    std::vector<int64_t> ticks;                             // sample ticks, strictly increasing
    std::vector<std::string> hashes;                        // state hash at each sample
    int keptEvery = 1;                                      // a bundle may keep one sample in N
    int sourceSamples = 0;
    std::vector<SetDef> sets;
    std::vector<FieldDef> fields;
    std::vector<ScenePoints> scene;
    std::vector<Stat> stats;
    Bounds bounds;
    ViewHint view;

    std::vector<Sample> samples;                 // samples.size() == ticks.size()
    std::vector<char> loaded;                    // per sample
    std::vector<std::vector<FieldFrame>> frames;  // per field, sorted by sample

    int sampleCount() const { return int(ticks.size()); }
    bool isLoaded(int k) const { return k >= 0 && k < int(loaded.size()) && loaded[size_t(k)]; }
    std::string metaValue(const std::string& key) const;

    /// The frame a field shows at sample k: its latest frame at or before k, or nullptr. A bundle
    /// restates every field at the start of each chunk, so a loaded sample always finds one.
    const FieldFrame* fieldAt(size_t field, int k) const;
};

/// Index of the last element of sorted `a` that is <= x, or -1.
int floorIndex(const std::vector<int64_t>& a, double x);

/// sRGB transfer function, linear 0..1 to display 0..1 (what usd-recorder's linear_to_srgb8 does,
/// without the rounding).
float linearToSrgb(float v);

}  // namespace rp
