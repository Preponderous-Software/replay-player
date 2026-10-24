// Playback: the clock, the current sample and the interpolation between samples, with the
// semantics of usd-web's player (viewer/usd-web-replay.js), so both look and behave the same on
// one bundle:
//
//  - the clock advances playbackRate x speed ticks per second; speeds are 0.25x to 8x;
//  - the head is a fractional sample index; its tick is interpolated between sample ticks;
//  - the current sample is the last one at or before the head (floorIndex);
//  - positions of ids present in both that sample and the next are interpolated, an id only in
//    the current sample is drawn where it was sampled (interpolation can be turned off);
//  - fields and stats hold their latest sample (the model does that);
//  - at an unloaded sample playback waits.
#pragma once

#include <array>
#include <vector>

#include "model/model.h"

namespace rp {

constexpr std::array<double, 6> kSpeeds = {0.25, 0.5, 1, 2, 4, 8};

class Playback {
public:
    explicit Playback(const ReplayModel& model) : model_(model) {}

    bool playing() const { return playing_; }
    double speed() const { return speed_; }
    double head() const { return head_; }
    bool waiting() const { return waiting_; }
    bool interpolate() const { return interpolate_; }
    bool loop() const { return loop_; }

    /// The current sample (floor of the head) and its tick.
    int sample() const;
    int64_t tick() const;
    /// The head's tick, interpolated between samples.
    double headTick() const;

    void play();  // from the start again when at the end
    void pause();
    void toggle() { playing_ ? pause() : play(); }
    void setSpeed(double s);
    void faster();  // the next speed in kSpeeds (wrapping, as usd-web's speed button does)
    void slower();
    void setInterpolate(bool on) { interpolate_ = on; }
    void setLoop(bool on) { loop_ = on; }

    void seekSample(double k);
    void seek(double tick);
    /// One sample forward or back (pauses, as usd-web's arrow keys do).
    void step(int delta);

    /// Advances the clock by `dt` seconds of wall time (clamped to 0.25 s, as usd-web does).
    /// Returns true when the head moved.
    bool advance(double dt);

private:
    const ReplayModel& model_;
    bool playing_ = false;
    bool waiting_ = false;
    bool interpolate_ = true;
    bool loop_ = true;
    double speed_ = 1.0;
    double head_ = 0.0;

    double tickToHead(double tick) const;
};

/// Positions of one set at the head: the current sample's points, moved towards the next
/// sample's for ids present in both. `scratch` is reused between calls (one slot per id).
void interpolatedPoints(const ReplayModel& model, size_t set, double head, bool interpolate,
                        std::vector<Vec3>& out, std::vector<int>& scratch);

}  // namespace rp
