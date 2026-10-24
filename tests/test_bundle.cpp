// The UWR1 decoder on a bundle usd-web made from usd-recorder's replay scenario
// (tests/data/scenario.usda: walkers 0.5 apart moving +0.25 per tick, glowers with vertex colours
// and widths, a 3 x 2 heat field resampled every 20 ticks).
#include <string>

#include "check.h"
#include "model/bundle.h"

using namespace rp;

int main(int argc, char** argv) {
    const std::string data = argc > 1 ? argv[1] : "tests/data";
    BundleIndex index;
    const ReplayModel m = loadBundleDir(data + "/scenario", &index);
    CHECK(m.sampleCount() == 13 && index.chunks.size() == 4);
    CHECK(m.ticks.front() == 0 && m.ticks.back() == 60);
    CHECK(m.hashes[1] == "0000000317156075");
    CHECK(m.title == "C++ \"scenario\"" && m.metaValue("seed") == "18446744073709551615");
    CHECK(m.playbackRate == 12);
    CHECK(m.sets.size() == 2 && m.sets[0].id == "walkers" && m.sets[1].shape == Shape::Cube);
    CHECK_NEAR(m.sets[0].color[0], 0x5a / 255.0, 1e-6);
    CHECK(m.sets[1].vertexColor && m.sets[1].vertexWidth && m.sets[1].idCount == 3);
    for (int k = 0; k < m.sampleCount(); ++k) CHECK(m.isLoaded(k));

    // Walkers at tick 10: ids 1..4 at (i * 0.5 + 2.5, -1.5 i, 0.125), within half a quant step.
    const SetSample& w = m.samples[2].sets[0];
    CHECK(w.points.size() == 4);
    for (size_t i = 0; i < w.points.size(); ++i) {
        const double id = 1 + double(i);
        CHECK_NEAR(w.points[i][0], id * 0.5 + 10 * 0.25, index.qstep[0]);
        CHECK_NEAR(w.points[i][1], -id * 1.5, index.qstep[1]);
        CHECK_NEAR(w.points[i][2], 0.125, index.qstep[2] + 1e-6);
    }
    // Dense ids: walker 1 was first seen at tick 0 as the second id.
    CHECK(m.samples[0].sets[0].ids[1] == 1 && m.samples[2].sets[0].ids[0] == 1);
    // Glowers: none before tick 20; vertex widths 0.25 * (1 + id % 3) back from u8.
    CHECK(m.samples[0].sets[1].ids.empty());
    const SetSample& g = m.samples[8].sets[1];  // tick 40: ids 100, 101
    CHECK(g.ids.size() == 2 && g.widths.size() == 2 && g.colors.size() == 2);
    CHECK_NEAR(g.widths[0], 0.25 * (1 + 100 % 3), 0.75 / 255);
    CHECK_NEAR(g.widths[1], 0.25 * (1 + 101 % 3), 0.75 / 255);

    // The field holds: one frame per chunk start plus each resample, and samples between them see
    // the latest frame at or before them.
    const FieldFrame* f = m.fieldAt(0, 3);  // tick 15: still the tick 0 colours
    CHECK(f && f->sample == 0 && f->rgba.size() == 6 * 4);
    CHECK(m.fieldAt(0, 4) && m.fieldAt(0, 4)->sample == 4);  // tick 20 resampled
    CHECK(m.fieldAt(0, 6)->rgba == m.fieldAt(0, 5)->rgba);   // tick 30 holds tick 25 (the chunk restates it)

    // Stats are held per sample; energy is sampled every other sample.
    CHECK(m.stats.size() == 2 && m.stats[0].name == "energy");
    CHECK(m.stats[0].values[1] && *m.stats[0].values[1] == 0);  // tick 5 holds tick 0's 0.0
    CHECK(m.stats[0].values[2] && *m.stats[0].values[2] == 15);

    // A thinned bundle: every third sample, the last kept.
    const ReplayModel t = loadBundleDir(data + "/scenario-every3");
    CHECK(t.sampleCount() == 5 && t.keptEvery == 3 && t.ticks[1] == 15 && t.ticks.back() == 60);
    CHECK(t.fieldAt(0, 1)->sample == 0);  // tick 15 holds tick 0's colours

    // Refusals: a newer version, another format, a bad chunk name, a damaged chunk.
    const std::string json = readAll(data + "/scenario/replay.json");
    auto replaced = [&](const std::string& from, const std::string& to) {
        std::string s = json;
        const size_t at = s.find(from);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) s.replace(at, from.size(), to);
        return s;
    };
    BundleIndex ix;
    CHECK_THROWS(openBundle(replaced("\"version\":1", "\"version\":2"), "x", ix));
    CHECK_THROWS(openBundle(replaced("usd-web-replay", "something-else"), "x", ix));
    CHECK_THROWS(openBundle(replaced(index.chunks[1].file, "../etc/passwd"), "x", ix));
    CHECK_THROWS(openBundle(replaced("\"samples\":13", "\"samples\":12"), "x", ix));
    const std::string gz = readAll(data + "/scenario/" + index.chunks[0].file);
    std::string truncated = gz.substr(0, gz.size() - 20);
    CHECK_THROWS(decodeChunk(reinterpret_cast<const uint8_t*>(truncated.data()), truncated.size(), m, index));
    const std::string raw = "UWR1\x01\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff";  // n = 4 billion entities
    CHECK_THROWS(decodeChunk(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), m, index));
    // A chunk applied at the wrong place is refused.
    const auto c0 = decodeChunk(reinterpret_cast<const uint8_t*>(gz.data()), gz.size(), m, index);
    ReplayModel copy = m;
    CHECK_THROWS(applyChunk(copy, index, 1, c0));
    return finish("bundle");
}
