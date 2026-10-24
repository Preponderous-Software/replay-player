// StrataScene: draws a ReplayModel at a playback head through Strata's C ABI (strata.h).
//
//   replay part    Strata
//   coordinates    replay (x, y, z), Z-up  ->  Strata (x, z, -y), +Y up (as usd-web maps to three.js)
//   entity set     strata_submit_primitives, sphere or cube per `replay:shape`, scale = width
//   field          one unlit textured quad (strata_add_textured_mesh) with a cols x rows texture,
//                  nearest-filtered (STRATA_TEXTURE_NEAREST, ABI 1.10), updated with
//                  strata_update_texture_rgba8 when the held frame changes
//   scene points   strata_submit_primitives every frame (static data)
//   lighting       STRATA_LIGHTING_SUN with a fixed sun, no fog; shadows optional
//   camera         an orbit around the replay's bounds (OrbitCamera)
#pragma once

#include <cstdint>
#include <vector>

#include "model/model.h"
#include "strata/strata.h"

namespace rp {

/// An orbit camera with usd-web's framing: the bounds' diagonal fits the narrower field of
/// view; `theta` turns about the up axis, `phi` is the angle down from straight above.
struct OrbitCamera {
    Vec3 target{0, 0, 0};  // Strata space
    double dist = 50;
    double theta = 0.6;
    double phi = 0.75;
    double minDist = 0.1;
    double maxDist = 1e6;
    double nearPlane = 0.05;
    double farPlane = 1e5;
    float fovY = 45.0f;

    void frame(const ReplayModel& model, double aspect, double padding = 0.0);
    void orbit(double dxPixels, double dyPixels);
    void pan(double dxPixels, double dyPixels);
    void zoom(double factor);
    StrataCamera camera(float aspect) const;
};

struct SceneOptions {
    bool shadows = true;
    float background[3] = {0x1b / 255.0f, 0x20 / 255.0f, 0x26 / 255.0f};  // usd-web's #1b2026
};

class StrataScene {
public:
    /// Creates the field textures and meshes. Throws std::runtime_error on a Strata failure.
    StrataScene(StrataEngine* engine, const ReplayModel& model, const SceneOptions& options);

    /// Updates field textures and submits the entities and scene points of the head. Call between
    /// strata_begin_frame and strata_end_frame. Returns the number of instances submitted.
    int draw(double head, bool interpolate);

    /// Re-applies lighting (after a change of options or bounds).
    void applyLighting();

    const SceneOptions& options() const { return options_; }
    void setShadows(bool on);

private:
    StrataEngine* engine_;
    const ReplayModel& model_;
    SceneOptions options_;
    struct FieldGpu {
        uint32_t texture = 0;
        uint32_t mesh = 0;
        int shown = -1;  // the sample of the frame on the texture
    };
    std::vector<FieldGpu> fields_;
    std::vector<StrataRenderItem> sceneItems_;
    std::vector<StrataRenderItem> items_;
    std::vector<Vec3> points_;
    std::vector<std::vector<int>> scratch_;
};

/// Throws std::runtime_error naming `what` and strata_last_error() unless status is STRATA_OK.
void check(StrataStatus status, const char* what);

}  // namespace rp
