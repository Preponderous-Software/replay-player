#include "play/playback.h"

#include <algorithm>
#include <cmath>

namespace rp {

int Playback::sample() const {
    const int n = model_.sampleCount();
    return std::max(0, std::min(n - 1, int(std::floor(head_))));
}

int64_t Playback::tick() const { return model_.ticks.empty() ? 0 : model_.ticks[size_t(sample())]; }

double Playback::headTick() const {
    const auto& t = model_.ticks;
    if (t.empty()) return 0;
    const int k = sample();
    const double f = head_ - k;
    return size_t(k) + 1 < t.size() ? double(t[size_t(k)]) + double(t[size_t(k) + 1] - t[size_t(k)]) * f
                                    : double(t.back());
}

double Playback::tickToHead(double tick) const {
    const auto& t = model_.ticks;
    const int k = floorIndex(t, tick);
    if (k < 0) return 0;
    if (size_t(k) + 1 >= t.size()) return double(t.size()) - 1;
    return k + (tick - double(t[size_t(k)])) / double(t[size_t(k) + 1] - t[size_t(k)]);
}

void Playback::play() {
    if (model_.sampleCount() == 0) return;
    if (head_ >= model_.sampleCount() - 1) head_ = 0;
    playing_ = true;
}

void Playback::pause() { playing_ = false; }

void Playback::setSpeed(double s) { speed_ = std::max(0.01, std::min(64.0, s)); }

void Playback::faster() {
    auto it = std::find(kSpeeds.begin(), kSpeeds.end(), speed_);
    speed_ = it == kSpeeds.end() || it + 1 == kSpeeds.end() ? kSpeeds.front() : *(it + 1);
}

void Playback::slower() {
    auto it = std::find(kSpeeds.begin(), kSpeeds.end(), speed_);
    speed_ = it == kSpeeds.end() || it == kSpeeds.begin() ? kSpeeds.back() : *(it - 1);
}

void Playback::seekSample(double k) {
    head_ = std::max(0.0, std::min(double(model_.sampleCount() - 1), k));
}

void Playback::seek(double tick) { seekSample(tickToHead(tick)); }

void Playback::step(int delta) {
    pause();
    seekSample(delta > 0 ? std::floor(head_) + delta : std::ceil(head_) + delta);
}

bool Playback::advance(double dt) {
    waiting_ = !model_.isLoaded(sample());
    if (!playing_ || waiting_ || model_.sampleCount() == 0) return false;
    dt = std::min(0.25, std::max(0.0, dt));
    const double t = headTick() + dt * model_.playbackRate * speed_;
    const double end = double(model_.ticks.back());
    if (t >= end) {
        if (loop_) {
            head_ = 0;
        } else {
            head_ = model_.sampleCount() - 1;
            pause();
        }
    } else {
        const double next = tickToHead(t);
        // Wait at an unloaded sample rather than skip it (a bundle streams in order).
        if (!model_.isLoaded(int(std::floor(next)))) {
            waiting_ = true;
            return false;
        }
        head_ = next;
    }
    return true;
}

void interpolatedPoints(const ReplayModel& model, size_t set, double head, bool interpolate,
                        std::vector<Vec3>& out, std::vector<int>& scratch) {
    out.clear();
    const int n = model.sampleCount();
    if (n == 0) return;
    const int k = std::max(0, std::min(n - 1, int(std::floor(head))));
    if (!model.isLoaded(k)) return;
    const SetSample& a = model.samples[size_t(k)].sets[set];
    out = a.points;
    const double frac = head - k;
    const int k2 = std::min(n - 1, k + 1);
    if (!interpolate || frac <= 0 || k2 == k || !model.isLoaded(k2)) return;
    const SetSample& b = model.samples[size_t(k2)].sets[set];
    uint32_t need = 0;
    for (uint32_t id : b.ids) need = std::max(need, id + 1);
    for (uint32_t id : a.ids) need = std::max(need, id + 1);
    if (scratch.size() < need) scratch.assign(need, -1);
    for (size_t j = 0; j < b.ids.size(); ++j) scratch[b.ids[j]] = int(j);
    const float f = float(frac);
    for (size_t i = 0; i < a.ids.size(); ++i) {
        const int j = scratch[a.ids[i]];
        if (j < 0) continue;
        for (size_t c = 0; c < 3; ++c) out[i][c] += (b.points[size_t(j)][c] - out[i][c]) * f;
    }
    for (uint32_t id : b.ids) scratch[id] = -1;
}

}  // namespace rp
