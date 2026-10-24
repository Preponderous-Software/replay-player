#include "render/scene.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "play/playback.h"

namespace rp {
namespace {

// Replay Z-up to Strata Y-up.
Vec3 toStrata(const Vec3& p) { return {p[0], p[2], -p[1]}; }

int32_t kind(Shape s) { return s == Shape::Cube ? STRATA_PRIMITIVE_CUBE : STRATA_PRIMITIVE_SPHERE; }

StrataRenderItem item(Shape shape, const Vec3& replayPos, float width, const Vec3& color) {
    StrataRenderItem it{};
    it.kind = kind(shape);
    const Vec3 p = toStrata(replayPos);
    it.position[0] = p[0];
    it.position[1] = p[1];
    it.position[2] = p[2];
    it.scale[0] = it.scale[1] = it.scale[2] = width;
    it.color[0] = color[0];
    it.color[1] = color[1];
    it.color[2] = color[2];
    it.color[3] = 1.0f;
    return it;
}

double diagonal(const Bounds& b) {
    return std::sqrt(double(b.max[0] - b.min[0]) * (b.max[0] - b.min[0]) + double(b.max[1] - b.min[1]) * (b.max[1] - b.min[1]) +
                     double(b.max[2] - b.min[2]) * (b.max[2] - b.min[2]));
}

}  // namespace

void check(StrataStatus status, const char* what) {
    if (status != STRATA_OK) throw std::runtime_error(std::string(what) + ": " + strata_last_error());
}

// ---------------------------------------------------------------- camera

void OrbitCamera::frame(const ReplayModel& model, double aspect, double padding) {
    const Bounds& b = model.bounds;
    const Vec3 c{(b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2, (b.min[2] + b.max[2]) / 2};
    target = toStrata(c);
    const double size = std::max({double(b.max[0] - b.min[0]), double(b.max[1] - b.min[1]), double(b.max[2] - b.min[2]), 1e-3});
    const double half = std::tan(fovY * 3.14159265358979323846 / 360.0);
    const double fit = std::min(half, half * aspect);
    const double radius = std::max(0.5 * diagonal(b), 1e-3);
    dist = radius / fit * (padding > 0 ? padding : (aspect < 1 ? 1.0 : 0.85));
    if (model.view.target) target = toStrata(*model.view.target);
    if (model.view.distance) dist = *model.view.distance / std::min(1.0, aspect);
    if (model.view.theta) theta = *model.view.theta;
    if (model.view.phi) phi = *model.view.phi;
    nearPlane = std::max(size / 5000.0, 1e-4);
    farPlane = size * 50.0;
    minDist = size * 0.05;
    maxDist = size * 8.0;
}

void OrbitCamera::orbit(double dx, double dy) {
    theta -= dx * 0.005;
    phi = std::max(0.05, std::min(1.5, phi - dy * 0.005));
}

void OrbitCamera::pan(double dx, double dy) {
    const double s = dist * 0.0015;
    const double fx = std::sin(theta), fz = std::cos(theta);  // forward on the ground
    const double rx = fz, rz = -fx;                            // right
    target[0] += float(-dx * s * rx - dy * s * fx);
    target[2] += float(-dx * s * rz - dy * s * fz);
}

void OrbitCamera::zoom(double factor) { dist = std::max(minDist, std::min(maxDist, dist * factor)); }

StrataCamera OrbitCamera::camera(float aspect) const {
    const double sp = std::sin(phi);
    const float eye[3] = {float(target[0] + dist * sp * std::sin(theta)), float(target[1] + dist * std::cos(phi)),
                          float(target[2] + dist * sp * std::cos(theta))};
    const float tgt[3] = {target[0], target[1], target[2]};
    const float up[3] = {0, 1, 0};
    StrataCamera cam{};
    // The far plane follows the orbit distance, so zooming out never clips the replay.
    const float farDist = float(std::max(farPlane, dist * 4.0));
    check(strata_camera_look_at(eye, tgt, up, fovY, aspect, float(nearPlane), farDist, &cam), "strata_camera_look_at");
    return cam;
}

// ---------------------------------------------------------------- scene

StrataScene::StrataScene(StrataEngine* engine, const ReplayModel& model, const SceneOptions& options)
    : engine_(engine), model_(model), options_(options) {
    if (strata_abi_minor() < 10) throw std::runtime_error("the Strata library is older than ABI 1.10 (nearest-texel textures)");
    for (const FieldDef& f : model.fields) {
        FieldGpu g;
        if (f.cols > STRATA_MAX_TEXTURE_SIZE || f.rows > STRATA_MAX_TEXTURE_SIZE || fields_.size() >= 200) {
            fields_.push_back(g);  // not drawn: larger than a texture can be
            continue;
        }
        std::vector<uint8_t> blank(size_t(f.cols) * size_t(f.rows) * 4, 40);
        for (size_t i = 3; i < blank.size(); i += 4) blank[i] = 255;
        check(strata_create_texture_rgba8(engine, f.cols, f.rows, blank.data(),
                                          STRATA_TEXTURE_NEAREST | STRATA_TEXTURE_CLAMP_U | STRATA_TEXTURE_CLAMP_V, &g.texture),
              "strata_create_texture_rgba8");
        // A quad over the grid, at the field's height. Corner (x0, y0) is the first texel row.
        const float x0 = float(f.origin[0]), y0 = float(f.origin[1]);
        const float x1 = float(f.origin[0] + f.cols * f.cellSize), y1 = float(f.origin[1] + f.rows * f.cellSize);
        const float z = float(f.z);
        const float positions[12] = {x0, z, -y0, x1, z, -y0, x1, z, -y1, x0, z, -y1};
        const float normals[12] = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
        const float uvs[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};  // counter-clockwise seen from above
        StrataTexturedMesh tm{};
        tm.mesh.positions = positions;
        tm.mesh.normals = normals;
        tm.mesh.indices = indices;
        tm.mesh.vertex_count = 4;
        tm.mesh.index_count = 6;
        tm.mesh.color[0] = tm.mesh.color[1] = tm.mesh.color[2] = tm.mesh.color[3] = 1.0f;
        tm.uvs = uvs;
        tm.texture = g.texture;
        tm.flags = STRATA_MESH_UNLIT;
        check(strata_add_textured_mesh(engine, &tm, &g.mesh), "strata_add_textured_mesh");
        fields_.push_back(g);
    }
    for (const ScenePoints& s : model.scene) {
        for (size_t i = 0; i < s.points.size(); ++i) {
            const Vec3& c = s.colors.size() > 1 ? s.colors[i] : s.colors[0];
            const float w = s.widths.size() > 1 ? s.widths[i] : s.widths[0];
            sceneItems_.push_back(item(s.shape, s.points[i], w, c));
        }
    }
    scratch_.assign(model.sets.size(), {});
    applyLighting();
}

void StrataScene::setShadows(bool on) {
    options_.shadows = on;
    applyLighting();
}

void StrataScene::applyLighting() {
    StrataLighting light{};
    check(strata_lighting_defaults(STRATA_LIGHTING_SUN, &light), "strata_lighting_defaults");
    light.fog = 0;
    light.cascade_count = options_.shadows ? 2 : 0;
    light.shadow_distance = float(std::max(10.0, diagonal(model_.bounds) * 2.0));
    check(strata_set_lighting(engine_, &light), "strata_set_lighting");
    const float toSun[3] = {0.4f, 1.0f, 0.6f};  // usd-web's directional light
    check(strata_set_sun_direction(engine_, toSun), "strata_set_sun_direction");
}

int StrataScene::draw(double head, bool interpolate) {
    const int n = model_.sampleCount();
    if (n == 0) return 0;
    const int k = std::max(0, std::min(n - 1, int(std::floor(head))));
    for (size_t fi = 0; fi < fields_.size(); ++fi) {
        FieldGpu& g = fields_[fi];
        if (!g.texture) continue;
        const FieldFrame* fr = model_.fieldAt(fi, k);
        if (!fr || fr->sample == g.shown) continue;
        const FieldDef& f = model_.fields[fi];
        check(strata_update_texture_rgba8(engine_, g.texture, 0, 0, f.cols, f.rows, fr->rgba.data()),
              "strata_update_texture_rgba8");
        g.shown = fr->sample;
    }
    items_.assign(sceneItems_.begin(), sceneItems_.end());
    if (model_.isLoaded(k)) {
        const Sample& sample = model_.samples[size_t(k)];
        for (size_t si = 0; si < model_.sets.size(); ++si) {
            const SetDef& def = model_.sets[si];
            const SetSample& e = sample.sets[si];
            interpolatedPoints(model_, si, head, interpolate, points_, scratch_[si]);
            for (size_t i = 0; i < points_.size(); ++i) {
                const Vec3& c = def.vertexColor && i < e.colors.size() ? e.colors[i] : def.color;
                const float w = def.vertexWidth && i < e.widths.size() ? e.widths[i] : def.width;
                items_.push_back(item(def.shape, points_[i], w, c));
            }
        }
    }
    if (!items_.empty()) {
        check(strata_submit_primitives(engine_, items_.data(), int32_t(items_.size())), "strata_submit_primitives");
    }
    return int(items_.size());
}

}  // namespace rp
