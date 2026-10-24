// NativeUi: the desktop player's HUD and input, drawn with Strata's own overlays (filled
// rectangles, ABI 1.11, and the 8 x 8 bitmap text). A panel shows the title, the run, the stats
// at the current tick, a legend in each set's colour and the state hash; a bar along the bottom
// has play/pause, a scrubber, the speed and the tick, laid out like usd-web's controls.
//
//   Space play/pause   Left/Right step a sample   +/- speed   Home/End first/last sample
//   I interpolation    L loop    H hide the HUD    R reset the view    F12 screenshot    Esc quit
//   drag orbit   right-drag or Shift+drag pan   wheel zoom   click or drag the scrubber to seek
#pragma once

#include <string>
#include <vector>

#include "model/model.h"
#include "play/playback.h"
#include "render/scene.h"
#include "strata/strata.h"

namespace rp {

struct UiActions {
    bool quit = false;
    bool screenshot = false;
    bool resetView = false;
};

class NativeUi {
public:
    NativeUi(const ReplayModel& model, Playback& playback, OrbitCamera& camera)
        : model_(model), playback_(playback), camera_(camera) {}

    /// Handles one event (keys, mouse, resize).
    void handle(const StrataEvent& e, UiActions& actions);

    /// Submits the HUD for this frame (between begin_frame and end_frame).
    void draw(StrataEngine* engine, int width, int height);

    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

private:
    const ReplayModel& model_;
    Playback& playback_;
    OrbitCamera& camera_;
    bool visible_ = true;
    int width_ = 0, height_ = 0;
    // Mouse state.
    bool dragOrbit_ = false, dragPan_ = false, dragScrub_ = false;
    float mx_ = 0, my_ = 0;
    int shift_ = 0;

    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };
    Rect bar_, play_, track_, speed_, panel_;
    void layout(int width, int height);
    void scrubTo(float x);
};

/// 1234567 -> "1,234,567".
std::string thousands(long long v);

}  // namespace rp
