#include "model/usd_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

#include "usd_recorder/replay_reader.hpp"

namespace rp {
namespace {

Vec3 srgb(const usd_recorder::Vec3f& lin) { return {linearToSrgb(lin[0]), linearToSrgb(lin[1]), linearToSrgb(lin[2])}; }

uint8_t byte(float v) { return uint8_t(std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f)); }

Shape shape(const std::string& s) { return s == "cube" ? Shape::Cube : Shape::Sphere; }

std::string metaText(const usd_recorder::MetaValue& v) {
    using K = usd_recorder::MetaValue::Kind;
    char buf[40];
    switch (v.kind) {
        case K::String: return v.s;
        case K::Bool: return v.b ? "true" : "false";
        case K::Int: return std::to_string(v.i);
        case K::UInt: return std::to_string(v.u);
        case K::Double:
            if (std::floor(v.d) == v.d && std::fabs(v.d) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", v.d);
            else std::snprintf(buf, sizeof buf, "%g", v.d);
            return buf;
        default: return {};
    }
}

struct Box {
    Vec3 lo{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity()};
    Vec3 hi{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()};
    void add(const Vec3& p) {
        for (size_t a = 0; a < 3; ++a) {
            if (!std::isfinite(p[a])) return;
        }
        for (size_t a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
        }
    }
};

}  // namespace

ReplayModel loadUsdReplay(const std::string& path) {
    const usd_recorder::Replay r = usd_recorder::read_replay(path);
    ReplayModel m;
    m.source = path;
    for (const auto& kv : r.meta) {
        const std::string t = metaText(kv.second);
        if (!t.empty()) m.meta.emplace_back(kv.first, t);
    }
    m.title = m.metaValue("title").empty() ? m.metaValue("sim") : m.metaValue("title");
    if (m.title.empty()) m.title = "replay";
    m.playbackRate = r.playback_rate > 0 ? r.playback_rate : 24.0;
    m.ticks = r.ticks;
    m.hashes = r.hashes;
    m.sourceSamples = int(r.ticks.size());
    const size_t n = r.ticks.size();
    m.samples.assign(n, {});
    m.loaded.assign(n, 1);
    Box box;

    for (size_t si = 0; si < r.sets.size(); ++si) {
        const usd_recorder::EntitySet& s = r.sets[si];
        SetDef d;
        d.id = s.name;
        d.label = s.label;
        d.shape = shape(s.shape);
        d.vertexColor = s.vertex_color;
        d.vertexWidth = s.vertex_width;
        if (!s.vertex_color) {
            d.color = !s.colors.empty() && !s.colors[0].empty() ? srgb(s.colors[0][0]) : Vec3{0.604f, 0.651f, 0.698f};
        }
        if (!s.vertex_width) d.width = !s.widths.empty() && !s.widths[0].empty() ? s.widths[0][0] : 1.0f;
        // Dense ids per set in first-seen order, as usd-web's encoder numbers them.
        std::unordered_map<int64_t, uint32_t> dense;
        for (size_t k = 0; k < n; ++k) {
            SetSample out;
            const auto& ids = s.ids[k];
            out.ids.reserve(ids.size());
            for (int64_t id : ids) {
                auto it = dense.emplace(id, uint32_t(dense.size())).first;
                out.ids.push_back(it->second);
            }
            out.points.resize(ids.size());
            for (size_t i = 0; i < ids.size(); ++i) {
                out.points[i] = {s.points[k][i][0], s.points[k][i][1], s.points[k][i][2]};
                box.add(out.points[i]);
            }
            if (s.vertex_color) {
                out.colors.reserve(ids.size());
                for (const auto& c : s.colors[k]) out.colors.push_back(srgb(c));
            }
            if (s.vertex_width) out.widths = s.widths[k];
            d.maxCount = std::max(d.maxCount, int(ids.size()));
            m.samples[k].sets.push_back(std::move(out));
        }
        d.idCount = uint32_t(dense.size());
        m.sets.push_back(std::move(d));
    }

    for (const usd_recorder::Field& f : r.fields) {
        FieldDef d;
        d.id = f.name;
        d.label = f.label;
        d.cols = f.cols;
        d.rows = f.rows;
        d.origin[0] = f.origin[0];
        d.origin[1] = f.origin[1];
        d.cellSize = f.cell_size;
        d.z = f.z;
        box.add({float(f.origin[0]), float(f.origin[1]), float(f.z)});
        box.add({float(f.origin[0] + f.cols * f.cell_size), float(f.origin[1] + f.rows * f.cell_size), float(f.z)});
        std::vector<FieldFrame> frames;
        for (size_t t = 0; t < f.ticks.size(); ++t) {
            const int k = floorIndex(m.ticks, double(f.ticks[t]));
            if (k < 0) continue;
            FieldFrame fr;
            fr.sample = k;
            fr.rgba.resize(f.colors[t].size() * 4);
            for (size_t i = 0; i < f.colors[t].size(); ++i) {
                const Vec3 c = srgb(f.colors[t][i]);
                fr.rgba[4 * i] = byte(c[0]);
                fr.rgba[4 * i + 1] = byte(c[1]);
                fr.rgba[4 * i + 2] = byte(c[2]);
                fr.rgba[4 * i + 3] = 255;
            }
            if (!frames.empty() && frames.back().sample == k) frames.back() = std::move(fr);
            else frames.push_back(std::move(fr));
        }
        m.fields.push_back(std::move(d));
        m.frames.push_back(std::move(frames));
    }

    for (const usd_recorder::Stat& st : r.stats) {
        Stat out;
        out.name = st.name;
        out.values.resize(n);
        size_t j = 0;
        for (size_t k = 0; k < n; ++k) {
            while (j < st.ticks.size() && st.ticks[j] <= m.ticks[k]) ++j;
            if (j > 0) out.values[k] = st.values[j - 1];
        }
        m.stats.push_back(std::move(out));
    }

    for (const usd_recorder::ScenePoints& s : r.scene) {
        ScenePoints p;
        p.id = s.name;
        p.label = s.label;
        p.shape = shape(s.shape);
        for (const auto& q : s.points) {
            p.points.push_back({q[0], q[1], q[2]});
            box.add(p.points.back());
        }
        for (const auto& c : s.colors) p.colors.push_back(srgb(c));
        p.widths = s.widths;
        if (p.colors.empty()) p.colors.push_back({0.5f, 0.5f, 0.5f});
        if (p.widths.empty()) p.widths.push_back(1.0f);
        m.scene.push_back(std::move(p));
    }

    if (std::isfinite(box.lo[0]) && box.lo[0] <= box.hi[0]) {
        m.bounds.min = box.lo;
        m.bounds.max = box.hi;
    }
    return m;
}

}  // namespace rp
