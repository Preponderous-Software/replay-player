// Playback semantics shared with usd-web's player: ticks per second, interpolation of ids present
// in both samples, stepping, seeking, looping and waiting at unloaded samples.
#include "check.h"
#include "play/playback.h"

using namespace rp;

namespace {

ReplayModel model() {
    ReplayModel m;
    m.playbackRate = 10;  // ticks per second
    m.ticks = {0, 10, 30};
    m.hashes = {"a", "b", "c"};
    m.sets.push_back(SetDef{});
    m.samples.resize(3);
    m.loaded.assign(3, 1);
    // Id 0 moves along x; id 1 exists only in sample 0; id 2 appears in sample 1.
    m.samples[0].sets.push_back(SetSample{{0, 1}, {{0, 0, 0}, {5, 5, 5}}, {}, {}});
    m.samples[1].sets.push_back(SetSample{{2, 0}, {{9, 9, 9}, {10, 0, 0}}, {}, {}});
    m.samples[2].sets.push_back(SetSample{{0}, {{30, 0, 0}}, {}, {}});
    return m;
}

}  // namespace

int main() {
    ReplayModel m = model();
    Playback p(m);
    CHECK(p.sample() == 0 && p.tick() == 0 && !p.playing());
    p.play();
    CHECK(p.advance(0.25));  // 2.5 ticks
    CHECK(p.advance(0.25));  // 5 ticks: halfway to the second sample
    CHECK_NEAR(p.head(), 0.5, 1e-9);
    CHECK_NEAR(p.headTick(), 5, 1e-9);
    std::vector<Vec3> pts;
    std::vector<int> scratch;
    interpolatedPoints(m, 0, p.head(), true, pts, scratch);
    CHECK(pts.size() == 2);
    CHECK_NEAR(pts[0][0], 5, 1e-6);  // id 0 interpolated 0 -> 10
    CHECK_NEAR(pts[1][0], 5, 1e-6);  // id 1 is not in the next sample: drawn where it was
    interpolatedPoints(m, 0, p.head(), false, pts, scratch);
    CHECK_NEAR(pts[0][0], 0, 1e-6);  // interpolation off
    p.advance(1.0);  // 0.25 s at most per step (as usd-web clamps): tick 7.5
    CHECK_NEAR(p.headTick(), 7.5, 1e-9);
    p.setSpeed(4);
    p.advance(0.25);  // 10 more ticks: 17.5
    CHECK_NEAR(p.headTick(), 17.5, 1e-9);
    CHECK(p.sample() == 1 && p.tick() == 10);
    p.advance(0.25);  // 27.5
    p.advance(0.25);  // past the end: loops to the start
    CHECK(p.head() == 0 && p.playing());
    p.setLoop(false);
    p.seek(29);
    p.advance(0.25);
    CHECK(p.sample() == 2 && !p.playing());  // stops at the end without a loop
    p.play();  // from the start again
    CHECK(p.head() == 0 && p.playing());
    p.step(+1);
    CHECK(!p.playing() && p.sample() == 1);
    p.step(-1);
    CHECK(p.sample() == 0);
    p.seek(20);
    CHECK_NEAR(p.head(), 1.5, 1e-9);
    p.seek(-5);
    CHECK(p.head() == 0);
    p.seek(1000);
    CHECK(p.head() == 2);
    // Speeds cycle through usd-web's steps.
    p.setSpeed(8);
    p.faster();
    CHECK(p.speed() == 0.25);
    p.slower();
    CHECK(p.speed() == 8);
    // An unloaded sample: playback waits there instead of skipping it.
    m.loaded[1] = 0;
    Playback q(m);
    q.play();
    CHECK(q.advance(0.25));  // tick 2.5: still in sample 0, which is loaded
    q.seekSample(0.9);       // tick 9
    CHECK(!q.advance(0.25));  // tick 11.5 would be in sample 1, which is not: wait
    CHECK(q.sample() == 0 && q.waiting());
    m.loaded[1] = 1;
    CHECK(q.advance(0.25) && q.sample() == 1 && !q.waiting());
    return finish("playback");
}
