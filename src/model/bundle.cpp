#include "model/bundle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "model/gunzip.h"
#include "model/json.h"

namespace rp {
namespace {

using json::Value;

constexpr size_t kMaxSets = 4096;
constexpr size_t kMaxFields = 256;  // Strata's mesh and texture limits are 256
constexpr int64_t kMaxFieldCells = int64_t(1) << 24;
constexpr size_t kMaxSamples = size_t(1) << 24;

[[noreturn]] void fail(const std::string& what) { throw BundleError("replay bundle: " + what); }

const Value& need(const Value& obj, const char* key) {
    const Value* v = obj.find(key);
    if (!v) fail(std::string("replay.json has no \"") + key + "\"");
    return *v;
}

double number(const Value& v, const char* what) {
    if (!v.isNumber()) fail(std::string(what) + " is not a number");
    return v.num;
}

int64_t integer(const Value& v, const char* what, int64_t lo, int64_t hi) {
    const double d = number(v, what);
    if (!(d >= double(lo) && d <= double(hi)) || std::floor(d) != d) fail(std::string(what) + " is out of range");
    return int64_t(d);
}

std::string text(const Value* v, const std::string& fallback) {
    return v && v->isString() && !v->str.empty() ? v->str : fallback;
}

Vec3 vec3(const Value& v, const char* what) {
    if (!v.isArray() || v.arr.size() != 3) fail(std::string(what) + " is not three numbers");
    return {float(number(v.arr[0], what)), float(number(v.arr[1], what)), float(number(v.arr[2], what))};
}

// "#rrggbb" to display colour 0..1.
Vec3 hexColor(const std::string& s) {
    auto hex = [&](size_t i) -> int {
        int v = 0;
        for (size_t k = i; k < i + 2; ++k) {
            const char c = s[k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail("bad colour \"" + s + "\"");
        }
        return v;
    };
    if (s.size() != 7 || s[0] != '#') fail("bad colour \"" + s + "\"");
    return {hex(1) / 255.0f, hex(3) / 255.0f, hex(5) / 255.0f};
}

Shape shape(const Value* v) { return v && v->isString() && v->str == "cube" ? Shape::Cube : Shape::Sphere; }

std::string metaText(const Value& v) {
    if (v.isString()) return v.str;
    if (v.isNumber()) {
        char buf[40];
        if (std::floor(v.num) == v.num && std::fabs(v.num) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", v.num);
        else std::snprintf(buf, sizeof buf, "%g", v.num);
        return buf;
    }
    if (v.type == Value::Type::Bool) return v.b ? "true" : "false";
    return {};
}

bool safeFileName(const std::string& f) {
    if (f.empty() || f[0] == '.' || f.size() > 200) return false;
    for (char c : f) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    size_t left() const { return n_ - off_; }
    size_t offset() const { return off_; }
    void need(uint64_t bytes) const {
        if (bytes > left()) fail("truncated replay chunk");
    }
    uint32_t u32() {
        need(4);
        const uint8_t* p = d_ + off_;
        off_ += 4;
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    uint16_t u16() {
        const uint8_t* p = d_ + off_;
        off_ += 2;
        return uint16_t(p[0] | p[1] << 8);
    }
    uint8_t u8() {
        need(1);
        return d_[off_++];
    }
    void bytes(std::vector<uint8_t>& out, size_t n) {
        need(n);
        out.assign(d_ + off_, d_ + off_ + n);
        off_ += n;
    }

private:
    const uint8_t* d_;
    size_t n_;
    size_t off_ = 0;
};

}  // namespace

int BundleIndex::chunkOf(int k) const {
    int lo = 0, hi = int(chunks.size()) - 1;
    while (lo < hi) {
        const int m = (lo + hi + 1) >> 1;
        if (chunks[size_t(m)].first <= k) lo = m;
        else hi = m - 1;
    }
    return lo;
}

ReplayModel openBundle(const std::string& replayJson, const std::string& source, BundleIndex& index) {
    Value doc;
    try {
        doc = json::parse(replayJson);
    } catch (const json::Error& e) {
        fail(e.what());
    }
    if (!doc.isObject()) fail("replay.json is not an object");
    const Value* format = doc.find("format");
    if (!format || !format->isString() || format->str != kBundleFormat) fail("not a usd-web replay (format)");
    const int64_t version = integer(need(doc, "version"), "version", 0, 1 << 20);
    if (version > kBundleVersion) {
        fail("replay version " + std::to_string(version) + " is newer than this player (" +
             std::to_string(kBundleVersion) + ")");
    }

    ReplayModel m;
    m.source = source;
    if (const Value* meta = doc.find("meta"); meta && meta->isObject()) {
        for (const auto& kv : meta->obj) {
            const std::string t = metaText(kv.second);
            if (!t.empty()) m.meta.emplace_back(kv.first, t);
        }
    }
    m.title = text(doc.find("title"), m.metaValue("title").empty() ? m.metaValue("sim") : m.metaValue("title"));
    if (m.title.empty()) m.title = "replay";
    m.playbackRate = number(need(doc, "playbackRate"), "playbackRate");
    if (!(m.playbackRate > 0 && m.playbackRate < 1e9)) fail("playbackRate is out of range");

    const Value& ticks = need(doc, "ticks");
    const Value& hashes = need(doc, "hashes");
    const int64_t samples = integer(need(doc, "samples"), "samples", 1, int64_t(kMaxSamples));
    if (!ticks.isArray() || !hashes.isArray() || int64_t(ticks.arr.size()) != samples ||
        int64_t(hashes.arr.size()) != samples) {
        fail("samples, ticks and hashes disagree");
    }
    for (size_t k = 0; k < ticks.arr.size(); ++k) {
        const int64_t t = integer(ticks.arr[k], "a tick", -(int64_t(1) << 53), int64_t(1) << 53);
        if (k && t <= m.ticks.back()) fail("ticks do not strictly increase");
        m.ticks.push_back(t);
        m.hashes.push_back(hashes.arr[k].isString() ? hashes.arr[k].str : std::string());
    }
    if (const Value* ke = doc.find("keptEvery"); ke && ke->isNumber()) m.keptEvery = int(integer(*ke, "keptEvery", 1, 1 << 30));
    if (const Value* ss = doc.find("sourceSamples"); ss && ss->isNumber()) {
        m.sourceSamples = int(integer(*ss, "sourceSamples", 0, int64_t(kMaxSamples) * 64));
    } else {
        m.sourceSamples = int(samples);
    }

    const Value& quant = need(doc, "quant");
    const Value& qmin = need(quant, "min");
    const Value& qstep = need(quant, "step");
    if (!qmin.isArray() || qmin.arr.size() != 3 || !qstep.isArray() || qstep.arr.size() != 3) fail("bad quant");
    for (int a = 0; a < 3; ++a) {
        index.qmin[a] = number(qmin.arr[size_t(a)], "quant.min");
        index.qstep[a] = number(qstep.arr[size_t(a)], "quant.step");
    }
    index.idBytes = integer(need(quant, "idBytes"), "quant.idBytes", 2, 4) == 4 ? 4 : 2;
    if (const Value* b = doc.find("bounds"); b && b->isObject()) {
        m.bounds.min = vec3(need(*b, "min"), "bounds.min");
        m.bounds.max = vec3(need(*b, "max"), "bounds.max");
    } else {
        m.bounds.min = {float(index.qmin[0]), float(index.qmin[1]), float(index.qmin[2])};
        for (int a = 0; a < 3; ++a) m.bounds.max[size_t(a)] = float(index.qmin[a] + index.qstep[a] * 65535.0);
    }

    const Value& sets = need(doc, "sets");
    if (!sets.isArray() || sets.arr.size() > kMaxSets) fail("bad sets");
    for (const Value& s : sets.arr) {
        SetDef d;
        d.id = text(s.find("id"), "set");
        d.label = text(s.find("label"), d.id);
        d.shape = shape(s.find("shape"));
        const Value* vc = s.find("vertexColor");
        const Value* vw = s.find("vertexWidth");
        d.vertexColor = vc && vc->type == Value::Type::Bool && vc->b;
        d.vertexWidth = vw && vw->type == Value::Type::Bool && vw->b;
        if (const Value* c = s.find("color"); c && c->isString()) d.color = hexColor(c->str);
        if (const Value* w = s.find("width"); w && w->isNumber()) d.width = float(w->num);
        double wm = 1.0;
        if (const Value* w = s.find("widthMax"); w && w->isNumber()) wm = w->num;
        index.widthMax.push_back(wm);
        if (const Value* mc = s.find("maxCount"); mc && mc->isNumber()) d.maxCount = int(integer(*mc, "maxCount", 0, 1 << 30));
        if (const Value* ids = s.find("ids"); ids && ids->isNumber()) {
            d.idCount = uint32_t(integer(*ids, "ids", 0, int64_t(1) << 32));
        }
        m.sets.push_back(std::move(d));
    }

    const Value& fields = need(doc, "fields");
    if (!fields.isArray() || fields.arr.size() > kMaxFields) fail("bad fields");
    for (const Value& f : fields.arr) {
        FieldDef d;
        d.id = text(f.find("id"), "field");
        d.label = text(f.find("label"), d.id);
        d.cols = int(integer(need(f, "cols"), "cols", 1, kMaxFieldCells));
        d.rows = int(integer(need(f, "rows"), "rows", 1, kMaxFieldCells));
        if (int64_t(d.cols) * d.rows > kMaxFieldCells) fail("a field is too large");
        const Value& o = need(f, "origin");
        if (!o.isArray() || o.arr.size() != 2) fail("bad field origin");
        d.origin[0] = number(o.arr[0], "origin");
        d.origin[1] = number(o.arr[1], "origin");
        d.cellSize = number(need(f, "cellSize"), "cellSize");
        if (const Value* z = f.find("z"); z && z->isNumber()) d.z = z->num;
        m.fields.push_back(std::move(d));
    }
    m.frames.assign(m.fields.size(), {});

    if (const Value* stats = doc.find("stats"); stats && stats->isObject()) {
        for (const auto& kv : stats->obj) {
            if (!kv.second.isArray() || int64_t(kv.second.arr.size()) != samples) fail("stat " + kv.first + " has the wrong length");
            Stat st;
            st.name = kv.first;
            for (const Value& v : kv.second.arr) {
                st.values.push_back(v.isNumber() ? std::optional<double>(v.num) : std::nullopt);
            }
            m.stats.push_back(std::move(st));
        }
    }

    if (const Value* scene = doc.find("scene"); scene && scene->isArray()) {
        for (const Value& s : scene->arr) {
            ScenePoints p;
            p.id = text(s.find("id"), "scene");
            p.label = text(s.find("label"), p.id);
            p.shape = shape(s.find("shape"));
            if (const Value* pts = s.find("points"); pts && pts->isArray()) {
                for (const Value& v : pts->arr) p.points.push_back(vec3(v, "scene point"));
            }
            if (const Value* cs = s.find("colors"); cs && cs->isArray()) {
                for (const Value& v : cs->arr) {
                    if (v.isString()) p.colors.push_back(hexColor(v.str));
                }
            }
            if (const Value* ws = s.find("widths"); ws && ws->isArray()) {
                for (const Value& v : ws->arr) p.widths.push_back(float(number(v, "scene width")));
            }
            if (p.colors.empty()) p.colors.push_back({0.5f, 0.5f, 0.5f});
            if (p.widths.empty()) p.widths.push_back(1.0f);
            if ((p.colors.size() > 1 && p.colors.size() != p.points.size()) ||
                (p.widths.size() > 1 && p.widths.size() != p.points.size())) {
                fail("scene " + p.id + ": colours or widths do not match its points");
            }
            m.scene.push_back(std::move(p));
        }
    }

    if (const Value* view = doc.find("view"); view && view->isObject()) {
        if (const Value* t = view->find("target")) m.view.target = vec3(*t, "view.target");
        if (const Value* d = view->find("distance"); d && d->isNumber()) m.view.distance = d->num;
        if (const Value* t = view->find("theta"); t && t->isNumber()) m.view.theta = t->num;
        if (const Value* p = view->find("phi"); p && p->isNumber()) m.view.phi = p->num;
    }

    const Value& chunks = need(doc, "chunks");
    if (!chunks.isArray() || chunks.arr.empty()) fail("no chunks");
    int next = 0;
    for (const Value& c : chunks.arr) {
        ChunkInfo ci;
        ci.file = text(c.find("file"), "");
        if (!safeFileName(ci.file)) fail("bad chunk file name \"" + ci.file + "\"");
        ci.first = int(integer(need(c, "first"), "chunk first", 0, samples - 1));
        ci.count = int(integer(need(c, "count"), "chunk count", 1, samples));
        if (const Value* b = c.find("bytes"); b && b->isNumber()) ci.bytes = uint64_t(integer(*b, "chunk bytes", 0, int64_t(1) << 40));
        if (ci.first != next) fail("chunks do not cover the samples in order");
        next = ci.first + ci.count;
        index.chunks.push_back(std::move(ci));
    }
    if (next != samples) fail("chunks do not cover every sample");

    m.samples.assign(size_t(samples), {});
    m.loaded.assign(size_t(samples), 0);
    return m;
}

DecodedChunk decodeChunk(const uint8_t* data, size_t size, const ReplayModel& model, const BundleIndex& index) {
    std::vector<uint8_t> raw;
    if (isGzip(data, size)) {
        try {
            raw = gunzip(data, size);
        } catch (const GunzipError& e) {
            fail(e.what());
        }
        data = raw.data();
        size = raw.size();
    }
    Reader in(data, size);
    in.need(12);
    if (std::memcmp(data, "UWR1", 4) != 0) fail("not a UWR1 chunk");
    in.u32();
    DecodedChunk out;
    out.count = in.u32();
    out.first = in.u32();
    if (out.count > model.ticks.size()) fail("chunk count out of range");
    const size_t idSize = size_t(index.idBytes);
    for (uint32_t s = 0; s < out.count; ++s) {
        DecodedSample sample;
        sample.sets.resize(model.sets.size());
        for (size_t si = 0; si < model.sets.size(); ++si) {
            const SetDef& def = model.sets[si];
            DecodedSet& e = sample.sets[si];
            const uint32_t n = in.u32();
            const uint64_t per = idSize + 6 + (def.vertexColor ? 3 : 0) + (def.vertexWidth ? 1 : 0);
            in.need(uint64_t(n) * per);  // before allocating anything
            e.ids.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t id = in.u16();
                if (idSize == 4) id |= uint32_t(in.u16()) << 16;
                if (def.idCount && id >= def.idCount) fail("an id is out of range");
                e.ids[i] = id;
            }
            e.q.resize(size_t(n) * 3);
            for (size_t i = 0; i < e.q.size(); ++i) e.q[i] = in.u16();
            if (def.vertexColor) in.bytes(e.colors, size_t(n) * 3);
            if (def.vertexWidth) in.bytes(e.widths, n);
        }
        for (const FieldDef& f : model.fields) {
            const uint8_t present = in.u8();
            if (present) {
                std::vector<uint8_t> rgb;
                in.bytes(rgb, size_t(f.cols) * size_t(f.rows) * 3);
                sample.fields.emplace_back(std::move(rgb));
            } else {
                sample.fields.emplace_back(std::nullopt);
            }
        }
        out.samples.push_back(std::move(sample));
    }
    if (in.left() != 0) fail("replay chunk has " + std::to_string(in.left()) + " trailing bytes");
    return out;
}

void applyChunk(ReplayModel& model, const BundleIndex& index, int chunk, const DecodedChunk& decoded) {
    if (chunk < 0 || size_t(chunk) >= index.chunks.size()) fail("no such chunk");
    const ChunkInfo& ci = index.chunks[size_t(chunk)];
    if (int64_t(decoded.first) != ci.first || int64_t(decoded.count) != ci.count) {
        fail("chunk " + ci.file + " does not hold the samples its index lists");
    }
    for (uint32_t s = 0; s < decoded.count; ++s) {
        const int k = int(decoded.first + s);
        const DecodedSample& ds = decoded.samples[s];
        Sample& sample = model.samples[size_t(k)];
        sample.sets.assign(model.sets.size(), {});
        for (size_t si = 0; si < model.sets.size(); ++si) {
            const DecodedSet& e = ds.sets[si];
            SetSample& out = sample.sets[si];
            const size_t n = e.ids.size();
            out.ids = e.ids;
            out.points.resize(n);
            for (size_t i = 0; i < n; ++i) {
                for (int a = 0; a < 3; ++a) {
                    out.points[i][size_t(a)] = float(index.qmin[a] + double(e.q[3 * i + size_t(a)]) * index.qstep[a]);
                }
            }
            if (!e.colors.empty()) {
                out.colors.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    out.colors[i] = {e.colors[3 * i] / 255.0f, e.colors[3 * i + 1] / 255.0f, e.colors[3 * i + 2] / 255.0f};
                }
            }
            if (!e.widths.empty()) {
                out.widths.resize(n);
                for (size_t i = 0; i < n; ++i) out.widths[i] = float(e.widths[i] / 255.0 * index.widthMax[si]);
            }
        }
        for (size_t fi = 0; fi < model.fields.size(); ++fi) {
            if (!ds.fields[fi]) continue;
            const std::vector<uint8_t>& rgb = *ds.fields[fi];
            FieldFrame fr;
            fr.sample = k;
            fr.rgba.resize(rgb.size() / 3 * 4);
            for (size_t i = 0, n = rgb.size() / 3; i < n; ++i) {
                fr.rgba[4 * i] = rgb[3 * i];
                fr.rgba[4 * i + 1] = rgb[3 * i + 1];
                fr.rgba[4 * i + 2] = rgb[3 * i + 2];
                fr.rgba[4 * i + 3] = 255;
            }
            auto& frames = model.frames[fi];
            auto it = std::lower_bound(frames.begin(), frames.end(), k,
                                       [](const FieldFrame& frame, int at) { return frame.sample < at; });
            if (it != frames.end() && it->sample == k) *it = std::move(fr);
            else frames.insert(it, std::move(fr));
        }
        model.loaded[size_t(k)] = 1;
    }
}

std::string decodedToJson(const DecodedChunk& decoded, const ReplayModel& model, const BundleIndex& index) {
    std::string o;
    char buf[48];
    auto num = [&](double v) {
        std::snprintf(buf, sizeof buf, "%.17g", v);
        o += buf;
    };
    auto integer = [&](uint64_t v) {
        std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(v));
        o += buf;
    };
    o += "{\"count\":";
    integer(decoded.count);
    o += ",\"first\":";
    integer(decoded.first);
    o += ",\"samples\":[";
    for (size_t s = 0; s < decoded.samples.size(); ++s) {
        const DecodedSample& ds = decoded.samples[s];
        if (s) o += ',';
        o += "{\"fields\":[";
        for (size_t fi = 0; fi < ds.fields.size(); ++fi) {
            if (fi) o += ',';
            if (!ds.fields[fi]) {
                o += "null";
                continue;
            }
            o += '[';
            const auto& rgb = *ds.fields[fi];
            for (size_t i = 0; i < rgb.size(); ++i) {
                if (i) o += ',';
                integer(rgb[i]);
            }
            o += ']';
        }
        o += "],\"sets\":[";
        for (size_t si = 0; si < ds.sets.size(); ++si) {
            const DecodedSet& e = ds.sets[si];
            if (si) o += ',';
            o += '{';
            if (model.sets[si].vertexColor) {
                o += "\"colors\":[";
                for (size_t i = 0; i < e.colors.size(); ++i) {
                    if (i) o += ',';
                    integer(e.colors[i]);
                }
                o += "],";
            }
            o += "\"ids\":[";
            for (size_t i = 0; i < e.ids.size(); ++i) {
                if (i) o += ',';
                integer(e.ids[i]);
            }
            o += "],\"points\":[";
            for (size_t i = 0; i < e.q.size(); ++i) {
                if (i) o += ',';
                num(index.qmin[i % 3] + double(e.q[i]) * index.qstep[i % 3]);
            }
            o += ']';
            if (model.sets[si].vertexWidth) {
                o += ",\"widths\":[";
                for (size_t i = 0; i < e.widths.size(); ++i) {
                    if (i) o += ',';
                    num(e.widths[i] / 255.0 * index.widthMax[si]);
                }
                o += ']';
            }
            o += '}';
        }
        o += "]}";
    }
    o += "]}";
    return o;
}

namespace {

std::string readFile(const std::string& path, size_t maxBytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) fail("cannot open " + path);
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0 || uint64_t(size) > maxBytes) fail(path + " is too large");
    f.seekg(0, std::ios::beg);
    std::string s(size_t(size), '\0');
    if (size > 0) f.read(&s[0], size);
    if (!f) fail("cannot read " + path);
    return s;
}

}  // namespace

ReplayModel loadBundleDir(const std::string& dir, BundleIndex* indexOut) {
    std::string base = dir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    BundleIndex index;
    ReplayModel m = openBundle(readFile(base + "replay.json", size_t(256) << 20), dir, index);
    for (size_t c = 0; c < index.chunks.size(); ++c) {
        const std::string bytes = readFile(base + index.chunks[c].file, size_t(1) << 30);
        const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
        applyChunk(m, index, int(c), decodeChunk(p, bytes.size(), m, index));
    }
    if (indexOut) *indexOut = std::move(index);
    return m;
}

}  // namespace rp
