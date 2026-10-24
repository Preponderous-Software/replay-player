// replay-player: plays any usd-replay/1 replay on the Strata engine.
//
//   replay-player PATH [options]
//
// PATH is a usd-web bundle (a directory holding replay.json and its UWR1 chunks, or the
// replay.json itself) or, in a build with -DREPLAY_PLAYER_WITH_USD=ON, a .usda or .usdc replay
// read with usd-recorder's C++ reader. See README.md for the options and the keys.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "model/bundle.h"
#include "model/model.h"
#include "play/playback.h"
#include "render/scene.h"
#include "strata/strata.h"
#include "ui/hud.h"
#ifdef REPLAY_PLAYER_WITH_USD
#include "model/usd_source.h"
#endif

#ifndef REPLAY_PLAYER_VERSION
#define REPLAY_PLAYER_VERSION "0.0.0"
#endif

namespace {

using namespace rp;

const char* kUsage =
    "usage: replay-player PATH [options]\n"
    "\n"
    "PATH: a usd-web replay bundle (a directory with replay.json and its chunks, or replay.json)"
#ifdef REPLAY_PLAYER_WITH_USD
    ",\n      or a usd-replay/1 .usda or .usdc file"
#else
    ".\n      (.usda/.usdc need a build with -DREPLAY_PLAYER_WITH_USD=ON)"
#endif
    "\n\n"
    "  --width W --height H   window or image size (default 1280 x 720)\n"
    "  --at TICK              start at this tick\n"
    "  --speed S              playback speed (0.25 .. 8; default 1)\n"
    "  --paused               start paused (a window starts playing otherwise)\n"
    "  --no-hud               hide the panel and the bar\n"
    "  --no-shadows           no sun shadows\n"
    "  --no-interpolate       draw each sample as recorded, without moving between samples\n"
    "  --theta R --phi R      camera angles in radians (around, and down from straight above)\n"
    "  --zoom F               multiply the framed camera distance by F\n"
    "  --spin DEG             turn the camera DEG degrees per second of playback (frames, window)\n"
    "  --headless             no window (EGL surfaceless on Linux); use with --screenshot or --frames\n"
    "  --screenshot FILE.png  draw one frame at --at, save it and exit\n"
    "  --frames DIR           save --count frames (PNG) of playback at --fps from --at, then exit\n"
    "  --count N --fps F      (with --frames; default 60 frames at 15 fps)\n"
    "  --info                 print a summary of the replay as JSON and exit (no GPU)\n"
    "  --version\n"
    "\n"
    "keys: Space play/pause, Left/Right step, +/- speed, Home/End, I interpolation, L loop,\n"
    "      H hide HUD, R reset view, F12 screenshot, Esc quit. Drag to orbit, right-drag to pan,\n"
    "      wheel to zoom, click or drag the scrubber to seek.\n";

struct Args {
    std::string path;
    int width = 1280, height = 720;
    bool hasAt = false;
    double at = 0;
    double speed = 1;
    bool paused = false, hud = true, shadows = true, interpolate = true, headless = false;
    bool hasTheta = false, hasPhi = false;
    double theta = 0, phi = 0, zoom = 1, spin = 0;
    std::string screenshot, frames;
    int count = 60;
    double fps = 15;
    bool info = false;
};

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string o = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "replay-player: %s needs a value\n", o.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        auto num = [&]() { return std::atof(next().c_str()); };
        if (o == "--help" || o == "-h") {
            std::fputs(kUsage, stdout);
            std::exit(0);
        } else if (o == "--version") {
            std::printf("replay-player %s (Strata %s)\n", REPLAY_PLAYER_VERSION, strata_version_string());
            std::exit(0);
        } else if (o == "--width") a.width = int(num());
        else if (o == "--height") a.height = int(num());
        else if (o == "--at") {
            a.hasAt = true;
            a.at = num();
        } else if (o == "--speed") a.speed = num();
        else if (o == "--paused") a.paused = true;
        else if (o == "--no-hud") a.hud = false;
        else if (o == "--no-shadows") a.shadows = false;
        else if (o == "--no-interpolate") a.interpolate = false;
        else if (o == "--theta") {
            a.hasTheta = true;
            a.theta = num();
        } else if (o == "--phi") {
            a.hasPhi = true;
            a.phi = num();
        } else if (o == "--zoom") a.zoom = num();
        else if (o == "--spin") a.spin = num();
        else if (o == "--headless") a.headless = true;
        else if (o == "--screenshot") a.screenshot = next();
        else if (o == "--frames") a.frames = next();
        else if (o == "--count") a.count = int(num());
        else if (o == "--fps") a.fps = num();
        else if (o == "--info") a.info = true;
        else if (!o.empty() && o[0] == '-') {
            std::fprintf(stderr, "replay-player: unknown option %s\n\n%s", o.c_str(), kUsage);
            std::exit(2);
        } else if (a.path.empty()) a.path = o;
        else {
            std::fprintf(stderr, "replay-player: one replay at a time\n");
            std::exit(2);
        }
    }
    if (a.path.empty()) {
        std::fputs(kUsage, stderr);
        std::exit(2);
    }
    if (a.width < 16 || a.height < 16 || a.width > 8192 || a.height > 8192) {
        std::fprintf(stderr, "replay-player: --width/--height out of range\n");
        std::exit(2);
    }
    return a;
}

std::string bundleDir(const std::string& path) {
    if (endsWith(path, "replay.json")) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? "." : path.substr(0, slash);
    }
    return path;
}

bool isUsd(const std::string& p) { return endsWith(p, ".usda") || endsWith(p, ".usdc") || endsWith(p, ".usd"); }

ReplayModel load(const Args& a) {
    if (isUsd(a.path)) {
#ifdef REPLAY_PLAYER_WITH_USD
        return loadUsdReplay(a.path);
#else
        throw std::runtime_error(a.path + ": this build cannot read .usd files (rebuild with -DREPLAY_PLAYER_WITH_USD=ON), "
                                          "or convert the replay with `usd-web replay` and open the bundle");
#endif
    }
    return loadBundleDir(bundleDir(a.path));
}

void jsonString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        } else {
            out += char(c);
        }
    }
    out += '"';
}

void printInfo(const ReplayModel& m) {
    std::string o = "{\"title\":";
    jsonString(o, m.title);
    o += ",\"samples\":" + std::to_string(m.sampleCount());
    o += ",\"ticks\":[" + std::to_string(m.ticks.front()) + "," + std::to_string(m.ticks.back()) + "]";
    o += ",\"firstHash\":";
    jsonString(o, m.hashes.front());
    o += ",\"lastHash\":";
    jsonString(o, m.hashes.back());
    o += ",\"sets\":{";
    for (size_t i = 0; i < m.sets.size(); ++i) {
        if (i) o += ',';
        jsonString(o, m.sets[i].id);
        o += ":{\"label\":";
        jsonString(o, m.sets[i].label);
        o += ",\"maxPoints\":" + std::to_string(m.sets[i].maxCount) + ",\"ids\":" + std::to_string(m.sets[i].idCount) + "}";
    }
    o += "},\"fields\":{";
    for (size_t i = 0; i < m.fields.size(); ++i) {
        if (i) o += ',';
        jsonString(o, m.fields[i].id);
        o += ":{\"size\":[" + std::to_string(m.fields[i].cols) + "," + std::to_string(m.fields[i].rows) +
             "],\"frames\":" + std::to_string(m.frames[i].size()) + "}";
    }
    o += "},\"stats\":[";
    for (size_t i = 0; i < m.stats.size(); ++i) {
        if (i) o += ',';
        jsonString(o, m.stats[i].name);
    }
    o += "],\"scene\":" + std::to_string(m.scene.size()) + "}\n";
    std::fputs(o.c_str(), stdout);
}

int run(const Args& a) {
    const auto t0 = std::chrono::steady_clock::now();
    ReplayModel model = load(a);
    const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (a.info) {
        printInfo(model);
        return 0;
    }
    std::fprintf(stderr, "replay-player: %s: %d samples, ticks %lld..%lld, loaded in %.0f ms\n", model.title.c_str(),
                 model.sampleCount(), (long long)model.ticks.front(), (long long)model.ticks.back(), loadMs);

    const bool offscreen = a.headless || !a.screenshot.empty() || !a.frames.empty();
    StrataEngineDesc desc{};
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION;
    desc.surface = offscreen ? int32_t(STRATA_SURFACE_HEADLESS) : int32_t(STRATA_SURFACE_WINDOW);
    desc.width = a.width;
    desc.height = a.height;
    desc.flags = offscreen ? 0u : uint32_t(STRATA_FLAG_VSYNC);
    const std::string title = "replay-player - " + model.title;
    desc.title = title.c_str();
    StrataEngine* raw = nullptr;
    check(strata_create(&desc, &raw), "strata_create");
    std::unique_ptr<StrataEngine, void (*)(StrataEngine*)> engine(raw, strata_destroy);

    SceneOptions so;
    so.shadows = a.shadows;
    StrataScene scene(engine.get(), model, so);
    Playback playback(model);
    playback.setSpeed(a.speed);
    playback.setInterpolate(a.interpolate);
    if (a.hasAt) playback.seek(a.at);
    OrbitCamera cam;
    auto frameCamera = [&](int w, int h) {
        cam.frame(model, double(w) / std::max(1, h));
        if (a.hasTheta) cam.theta = a.theta;
        if (a.hasPhi) cam.phi = a.phi;
        cam.dist *= a.zoom;
    };
    frameCamera(a.width, a.height);
    NativeUi ui(model, playback, cam);
    ui.setVisible(a.hud);

    auto drawFrame = [&]() {
        int32_t w = 0, h = 0;
        check(strata_get_drawable_size(engine.get(), &w, &h), "strata_get_drawable_size");
        const StrataCamera c = cam.camera(float(w) / float(std::max(1, h)));
        check(strata_set_camera(engine.get(), &c), "strata_set_camera");
        StrataFrameDesc fd{};
        fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
        for (int i = 0; i < 3; ++i) fd.clear_color[i] = so.background[i];
        fd.clear_color[3] = 1;
        check(strata_begin_frame(engine.get(), &fd), "strata_begin_frame");
        scene.draw(playback.head(), playback.interpolate());
        ui.draw(engine.get(), w, h);
        check(strata_end_frame(engine.get()), "strata_end_frame");
        check(strata_present(engine.get()), "strata_present");
    };

    if (!a.screenshot.empty()) {
        drawFrame();
        check(strata_save_screenshot_png(engine.get(), a.screenshot.c_str()), "strata_save_screenshot_png");
        std::fprintf(stderr, "replay-player: tick %lld, state %s -> %s\n", (long long)playback.tick(),
                     model.hashes[size_t(playback.sample())].c_str(), a.screenshot.c_str());
        return 0;
    }
    if (!a.frames.empty()) {
        playback.setLoop(false);
        if (!a.paused) playback.play();
        const double dt = 1.0 / std::max(1.0, a.fps);
        for (int f = 0; f < a.count; ++f) {
            drawFrame();
            char name[64];
            std::snprintf(name, sizeof name, "/frame_%04d.png", f);
            check(strata_save_screenshot_png(engine.get(), (a.frames + name).c_str()), "strata_save_screenshot_png");
            playback.advance(dt);
            cam.theta += a.spin * 3.14159265358979323846 / 180.0 * dt;
        }
        std::fprintf(stderr, "replay-player: %d frames to %s, last tick %lld\n", a.count, a.frames.c_str(),
                     (long long)playback.tick());
        return 0;
    }

    if (!a.paused) playback.play();
    auto last = std::chrono::steady_clock::now();
    int shot = 0;
    for (;;) {
        StrataEvent e;
        UiActions act;
        while (strata_poll_event(engine.get(), &e)) {
            if (e.type == STRATA_EVENT_RESIZED) frameCamera(e.width, e.height);
            ui.handle(e, act);
        }
        if (act.quit) break;
        if (act.resetView) {
            int32_t w = 0, h = 0;
            strata_get_drawable_size(engine.get(), &w, &h);
            frameCamera(w, h);
        }
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        playback.advance(dt);
        if (playback.playing()) cam.theta += a.spin * 3.14159265358979323846 / 180.0 * dt;
        drawFrame();
        if (act.screenshot) {
            char name[64];
            std::snprintf(name, sizeof name, "replay-player-%03d.png", shot++);
            strata_save_screenshot_png(engine.get(), name);
            std::fprintf(stderr, "replay-player: saved %s\n", name);
        }
    }
    return 0;
}

}  // namespace

// Leaves with std::_Exit once everything is flushed: after OpenUSD's text parser has failed on a
// damaged .usda, process teardown can abort inside OpenUSD (usd-recorder's ARCHITECTURE.md, "A
// shutdown trap"), and the engine is already destroyed by then.
int main(int argc, char** argv) {
    const Args a = parse(argc, argv);
    int code = 1;
    try {
        code = run(a);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "replay-player: %s\n", e.what());
    }
    std::fflush(nullptr);
    std::_Exit(code);
}
