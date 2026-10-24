// replay-compare: a .usda/.usdc replay read natively (usd-recorder's C++ reader) and the usd-web
// bundle made from it (`usd-web replay FILE -o DIR`, no thinning) give the same model, up to the
// bundle's quantization.
//
//   replay-compare REPLAY.usdc BUNDLE_DIR      exit 0 when they agree; the first differences otherwise
//
// Checked: sample ticks and state hashes (exact), sets and their dense ids (exact), positions
// (within half a quantization step), colours (within one sRGB byte), widths (within one byte of
// widthMax), every field frame (within one byte per channel) and stats (to the bundle's six
// significant digits).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>

#include "model/bundle.h"
#include "model/usd_source.h"

using namespace rp;

namespace {

int g_problems = 0;

void problem(const std::string& what) {
    if (++g_problems <= 20) std::fprintf(stderr, "  %s\n", what.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: replay-compare REPLAY.usd[a|c] BUNDLE_DIR\n");
        return 2;
    }
    int code = 1;
    try {
        const ReplayModel u = loadUsdReplay(argv[1]);
        BundleIndex index;
        const ReplayModel b = loadBundleDir(argv[2], &index);
        if (u.ticks != b.ticks) problem("sample ticks differ (was the bundle thinned?)");
        if (u.hashes != b.hashes) problem("state hashes differ");
        if (u.sets.size() != b.sets.size()) problem("set counts differ");
        if (u.fields.size() != b.fields.size()) problem("field counts differ");
        const size_t n = std::min(u.ticks.size(), b.ticks.size());
        double worst[3] = {0, 0, 0};
        for (size_t si = 0; si < std::min(u.sets.size(), b.sets.size()); ++si) {
            const SetDef &du = u.sets[si], &db = b.sets[si];
            if (du.id != db.id || du.label != db.label || du.shape != db.shape || du.vertexColor != db.vertexColor ||
                du.vertexWidth != db.vertexWidth || du.idCount != db.idCount) {
                problem("set " + du.id + ": definitions differ");
            }
            for (int c = 0; c < 3; ++c) {
                if (!du.vertexColor && std::fabs(du.color[size_t(c)] - db.color[size_t(c)]) > 1.0 / 255 + 1e-6) {
                    problem("set " + du.id + ": constant colour differs");
                }
            }
            for (size_t k = 0; k < n; ++k) {
                const SetSample &a = u.samples[k].sets[si], &c = b.samples[k].sets[si];
                const std::string at = "set " + du.id + " sample " + std::to_string(k);
                if (a.ids != c.ids) {
                    problem(at + ": ids differ");
                    continue;
                }
                for (size_t i = 0; i < a.points.size(); ++i) {
                    for (int ax = 0; ax < 3; ++ax) {
                        const double d = std::fabs(double(a.points[i][size_t(ax)]) - c.points[i][size_t(ax)]);
                        worst[ax] = std::max(worst[ax], index.qstep[ax] > 0 ? d / index.qstep[ax] : d);
                        if (d > index.qstep[ax] * 0.5 + 1e-5 * (1 + std::fabs(a.points[i][size_t(ax)]))) {
                            problem(at + ": a position is off by more than half a quantization step");
                        }
                    }
                    if (du.vertexColor) {
                        for (size_t ch = 0; ch < 3; ++ch) {
                            if (std::fabs(a.colors[i][ch] - c.colors[i][ch]) > 1.0 / 255 + 1e-6) problem(at + ": a colour differs");
                        }
                    }
                    if (du.vertexWidth && std::fabs(a.widths[i] - c.widths[i]) > index.widthMax[si] / 255 + 1e-6) {
                        problem(at + ": a width differs");
                    }
                }
            }
        }
        for (size_t fi = 0; fi < std::min(u.fields.size(), b.fields.size()); ++fi) {
            for (size_t k = 0; k < n; ++k) {
                const FieldFrame *a = u.fieldAt(fi, int(k)), *c = b.fieldAt(fi, int(k));
                if (!a || !c || a->rgba.size() != c->rgba.size()) {
                    problem("field " + u.fields[fi].id + " sample " + std::to_string(k) + ": no frame on one side");
                    continue;
                }
                for (size_t i = 0; i < a->rgba.size(); ++i) {
                    if (std::abs(int(a->rgba[i]) - int(c->rgba[i])) > 1) {
                        problem("field " + u.fields[fi].id + " sample " + std::to_string(k) + ": colours differ");
                        break;
                    }
                }
            }
        }
        for (const Stat& su : u.stats) {
            auto it = std::find_if(b.stats.begin(), b.stats.end(), [&](const Stat& s) { return s.name == su.name; });
            if (it == b.stats.end()) {
                problem("stat " + su.name + " missing from the bundle");
                continue;
            }
            for (size_t k = 0; k < n; ++k) {
                const auto &x = su.values[k], &y = it->values[k];
                if (bool(x) != bool(y) || (x && std::fabs(*x - *y) > 1e-5 * std::max(1.0, std::fabs(*x)))) {
                    problem("stat " + su.name + " sample " + std::to_string(k) + " differs");
                    break;
                }
            }
        }
        std::printf("%s vs %s: %zu samples, %zu sets, %zu fields, %zu stats; worst position error %.3f / %.3f / %.3f "
                    "quantization steps; %d problem(s)\n",
                    argv[1], argv[2], n, u.sets.size(), u.fields.size(), u.stats.size(), worst[0], worst[1], worst[2],
                    g_problems);
        code = g_problems ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "replay-compare: %s\n", e.what());
    }
    std::fflush(nullptr);
    std::_Exit(code);  // see app/main.cpp: OpenUSD's teardown after a parse failure
}
