// replay-player in a browser: the same model, playback and Strata scene as the desktop player,
// driven by web/index.html.
//
// The page owns everything a browser is better at: it fetches replay.json and the chunks,
// gunzips them with DecompressionStream, draws the controls as DOM elements (Strata's events have
// no touch, and its text is an 8 x 8 bitmap font) and turns pointer, pinch and wheel input into
// rp_orbit / rp_pan / rp_zoom. It calls rp_frame from requestAnimationFrame. This file exports
// that small C surface. Strata draws into a WebGL 2 context made here on the page's canvas
// (STRATA_SURFACE_HOST_CONTEXT), so SDL never takes the page's keyboard or pointer.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "model/bundle.h"
#include "play/playback.h"
#include "render/scene.h"
#include "strata/strata.h"

using namespace rp;

namespace {

struct Player {
    ReplayModel model;
    BundleIndex index;
    std::unique_ptr<Playback> playback;
    std::unique_ptr<StrataScene> scene;
    OrbitCamera camera;
    bool shadows = true;
    bool dirty = true;
    long frames = 0;
};

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_context = 0;
StrataEngine* g_engine = nullptr;
std::unique_ptr<Player> g_player;
std::string g_error, g_state;
int g_width = 640, g_height = 480;

void* noProc(const char*, void*) { return nullptr; }  // never called in a browser (WEB.md)

bool ensureEngine() {
    if (g_engine) return true;
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = true;
    attrs.antialias = true;
    attrs.preserveDrawingBuffer = false;
    g_context = emscripten_webgl_create_context("#view", &attrs);
    if (g_context <= 0) {
        g_error = "this browser has no WebGL 2";
        return false;
    }
    emscripten_webgl_make_context_current(g_context);
    emscripten_set_canvas_element_size("#view", g_width, g_height);
    StrataEngineDesc desc{};
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION;
    desc.surface = STRATA_SURFACE_HOST_CONTEXT;
    desc.width = g_width;
    desc.height = g_height;
    desc.get_proc_address = noProc;
    if (strata_create(&desc, &g_engine) != STRATA_OK) {
        g_error = std::string("strata_create: ") + strata_last_error();
        g_engine = nullptr;
        return false;
    }
    return true;
}

void frameCamera() {
    if (!g_player) return;
    g_player->camera.frame(g_player->model, double(g_width) / std::max(1, g_height));
}

void jsonString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c >= 0x20) {
            out += char(c);
        }
    }
    out += '"';
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* rp_last_error() { return g_error.c_str(); }

/// Opens a replay from its replay.json text. 0 on success; otherwise rp_last_error() says why.
EMSCRIPTEN_KEEPALIVE int rp_open(const char* json, int length) {
    try {
        if (!ensureEngine()) return 1;
        g_player.reset();
        strata_clear_meshes(g_engine);
        auto p = std::make_unique<Player>();
        p->model = openBundle(std::string(json, size_t(length)), "replay.json", p->index);
        g_player = std::move(p);
        g_player->playback = std::make_unique<Playback>(g_player->model);
        SceneOptions so;
        so.shadows = g_player->shadows;
        g_player->scene = std::make_unique<StrataScene>(g_engine, g_player->model, so);
        frameCamera();
        return 0;
    } catch (const std::exception& e) {
        g_error = e.what();
        g_player.reset();
        return 1;
    }
}

/// Adds chunk `index` (gunzipped or gzip'd bytes). 0 on success.
EMSCRIPTEN_KEEPALIVE int rp_add_chunk(int index, const uint8_t* bytes, int length) {
    if (!g_player) return 1;
    try {
        const DecodedChunk c = decodeChunk(bytes, size_t(length), g_player->model, g_player->index);
        applyChunk(g_player->model, g_player->index, index, c);
        g_player->dirty = true;
        return 0;
    } catch (const std::exception& e) {
        g_error = e.what();
        return 1;
    }
}

EMSCRIPTEN_KEEPALIVE void rp_play() {
    if (g_player) g_player->playback->play(), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_pause() {
    if (g_player) g_player->playback->pause(), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_seek(double tick) {
    if (g_player) g_player->playback->seek(tick), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_seek_sample(double k) {
    if (g_player) g_player->playback->seekSample(k), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_step(int delta) {
    if (g_player) g_player->playback->step(delta), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_set_speed(double s) {
    if (g_player) g_player->playback->setSpeed(s);
}
EMSCRIPTEN_KEEPALIVE void rp_set_interpolate(int on) {
    if (g_player) g_player->playback->setInterpolate(on != 0), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_set_shadows(int on) {
    if (g_player) {
        g_player->shadows = on != 0;
        if (g_player->scene) g_player->scene->setShadows(on != 0);
        g_player->dirty = true;
    }
}
EMSCRIPTEN_KEEPALIVE void rp_orbit(double dx, double dy) {
    if (g_player) g_player->camera.orbit(dx, dy), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_pan(double dx, double dy) {
    if (g_player) g_player->camera.pan(dx, dy), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_zoom(double factor) {
    if (g_player) g_player->camera.zoom(factor), g_player->dirty = true;
}
EMSCRIPTEN_KEEPALIVE void rp_reset_view() {
    frameCamera();
    if (g_player) g_player->dirty = true;
}

/// The canvas's new size in device pixels; `reframe` fits the camera to the replay again.
EMSCRIPTEN_KEEPALIVE void rp_resize(int width, int height, int reframe) {
    g_width = std::max(1, width);
    g_height = std::max(1, height);
    emscripten_set_canvas_element_size("#view", g_width, g_height);
    if (g_engine) strata_resize(g_engine, g_width, g_height);
    if (reframe) frameCamera();
    if (g_player) g_player->dirty = true;
}

/// Advances playback by dt seconds and draws when anything changed (or `force`). Returns 1 when
/// it drew a frame.
EMSCRIPTEN_KEEPALIVE int rp_frame(double dt, int force) {
    if (!g_player || !g_engine) return 0;
    Player& p = *g_player;
    const bool moved = p.playback->advance(dt);
    if (!(moved || p.dirty || force || p.playback->waiting())) return 0;
    p.dirty = false;
    try {
        emscripten_webgl_make_context_current(g_context);
        const StrataCamera cam = p.camera.camera(float(g_width) / float(std::max(1, g_height)));
        check(strata_set_camera(g_engine, &cam), "strata_set_camera");
        StrataFrameDesc fd{};
        fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
        const SceneOptions so;
        for (int i = 0; i < 3; ++i) fd.clear_color[i] = so.background[i];
        fd.clear_color[3] = 1;
        check(strata_begin_frame(g_engine, &fd), "strata_begin_frame");
        p.scene->draw(p.playback->head(), p.playback->interpolate());
        check(strata_end_frame(g_engine), "strata_end_frame");
        strata_present(g_engine);
        ++p.frames;
        return 1;
    } catch (const std::exception& e) {
        g_error = e.what();
        return 0;
    }
}

/// The playback state as JSON: playing, waiting, speed, sample, head, tick, hash, loaded (samples),
/// frames, interpolate, shadows and the held stats of the current sample.
EMSCRIPTEN_KEEPALIVE const char* rp_state() {
    g_state = "{";
    if (g_player) {
        const Player& p = *g_player;
        const Playback& pb = *p.playback;
        const int k = pb.sample();
        int loaded = 0;
        for (char c : p.model.loaded) loaded += c ? 1 : 0;
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "\"playing\":%s,\"waiting\":%s,\"speed\":%g,\"sample\":%d,\"head\":%.6f,\"tick\":%lld,\"loaded\":%d,"
                      "\"frames\":%ld,\"interpolate\":%s,\"shadows\":%s,\"hash\":",
                      pb.playing() ? "true" : "false", pb.waiting() ? "true" : "false", pb.speed(), k, pb.head(),
                      (long long)pb.tick(), loaded, p.frames, pb.interpolate() ? "true" : "false",
                      p.shadows ? "true" : "false");
        g_state += buf;
        jsonString(g_state, size_t(k) < p.model.hashes.size() ? p.model.hashes[size_t(k)] : "");
        g_state += ",\"stats\":{";
        for (size_t i = 0; i < p.model.stats.size(); ++i) {
            if (i) g_state += ',';
            jsonString(g_state, p.model.stats[i].name);
            const auto& v = size_t(k) < p.model.stats[i].values.size() ? p.model.stats[i].values[size_t(k)] : std::nullopt;
            if (v && std::isfinite(*v)) {
                std::snprintf(buf, sizeof buf, ":%.17g", *v);
                g_state += buf;
            } else {
                g_state += ":null";
            }
        }
        g_state += "}";
    }
    g_state += "}";
    return g_state.c_str();
}

}  // extern "C"

int main() {
    // Nothing to do until the page calls rp_open: the runtime stays alive (EXIT_RUNTIME=0).
    return 0;
}
