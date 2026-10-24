#include "model/model.h"

#include <algorithm>
#include <cmath>

namespace rp {

std::string ReplayModel::metaValue(const std::string& key) const {
    for (const auto& kv : meta) {
        if (kv.first == key) return kv.second;
    }
    return {};
}

const FieldFrame* ReplayModel::fieldAt(size_t field, int k) const {
    if (field >= frames.size()) return nullptr;
    const auto& f = frames[field];
    auto it = std::upper_bound(f.begin(), f.end(), k, [](int s, const FieldFrame& fr) { return s < fr.sample; });
    if (it == f.begin()) return nullptr;
    return &*(it - 1);
}

int floorIndex(const std::vector<int64_t>& a, double x) {
    int lo = 0, hi = int(a.size()) - 1, r = -1;
    while (lo <= hi) {
        const int m = (lo + hi) >> 1;
        if (double(a[size_t(m)]) <= x) {
            r = m;
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return r;
}

float linearToSrgb(float v) {
    v = std::min(1.0f, std::max(0.0f, v));
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

}  // namespace rp
