#include "ui/hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rp {
namespace {

// SDL3 / USB HID scancodes.
enum : int32_t {
    kKeyH = 11,
    kKeyI = 12,
    kKeyL = 15,
    kKeyQ = 20,
    kKeyR = 21,
    kKeyEscape = 41,
    kKeyMinus = 45,
    kKeyEquals = 46,
    kKeySpace = 44,
    kKeyF12 = 69,
    kKeyHome = 74,
    kKeyEnd = 77,
    kKeyRight = 79,
    kKeyLeft = 80,
    kKeyKpMinus = 86,
    kKeyKpPlus = 87,
};
constexpr int32_t kShiftMask = 0x0003;

const float kPanel[4] = {0.086f, 0.106f, 0.133f, 0.88f};
const float kChip[4] = {0.137f, 0.165f, 0.2f, 1.0f};
const float kInk[4] = {0.902f, 0.929f, 0.953f, 1.0f};
const float kMuted[4] = {0.604f, 0.651f, 0.698f, 1.0f};
const float kAccent[4] = {0.431f, 0.659f, 1.0f, 1.0f};
const float kTrack[4] = {0.3f, 0.34f, 0.4f, 1.0f};

StrataRect rect(float x, float y, float w, float h, const float c[4]) {
    StrataRect r{};
    r.x = x;
    r.y = y;
    r.width = std::max(0.0f, w);
    r.height = std::max(0.0f, h);
    for (int i = 0; i < 4; ++i) r.color[i] = c[i];
    return r;
}

// Strata's font is printable ASCII: anything else would show as '?', so it is dropped or replaced.
std::string ascii(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x20 && c < 0x7f) out += char(c);
        else if (c == 0xc2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xb7) {
            out += '-';  // the middle dot usd-web uses as a separator
            ++i;
        } else if (c >= 0x80 && (c & 0xc0) != 0x80) {
            out += '?';
        }
    }
    return out;
}

std::string fit(const std::string& s, size_t chars) {
    if (s.size() <= chars) return s;
    return chars > 3 ? s.substr(0, chars - 3) + "..." : s.substr(0, chars);
}

std::string statText(double v) {
    char buf[48];
    if (std::floor(v) == v && std::fabs(v) < 1e15) return thousands((long long)v);
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

}  // namespace

std::string thousands(long long v) {
    std::string digits = std::to_string(v < 0 ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return v < 0 ? "-" + out : out;
}

void NativeUi::layout(int width, int height) {
    width_ = width;
    height_ = height;
    const float m = 12, h = 44;
    bar_ = {m, float(height) - m - h, float(width) - 2 * m, h};
    play_ = {bar_.x + 8, bar_.y + 6, 72, h - 12};
    speed_ = {bar_.x + bar_.w - 8 - 56, bar_.y + 6, 56, h - 12};
    // The tick readout, beside the speed, as wide as its longest text.
    const float px = height >= 600 ? 16.0f : 8.0f;
    const std::string longest = "tick " + thousands(model_.ticks.empty() ? 0 : model_.ticks.back()) + " / " +
                                thousands(model_.ticks.empty() ? 0 : model_.ticks.back());
    const float tickW = width >= 900 ? px * float(longest.size()) + 16 : 0.0f;
    track_ = {play_.x + play_.w + 16, bar_.y + 6, speed_.x - 16 - tickW - (play_.x + play_.w + 16), h - 12};
}

void NativeUi::scrubTo(float x) {
    const int n = model_.sampleCount();
    if (n <= 1 || track_.w <= 0) return;
    const float f = std::max(0.0f, std::min(1.0f, (x - track_.x) / track_.w));
    playback_.seekSample(std::round(f * float(n - 1)));
}

void NativeUi::handle(const StrataEvent& e, UiActions& actions) {
    switch (e.type) {
        case STRATA_EVENT_QUIT: actions.quit = true; break;
        case STRATA_EVENT_KEY_DOWN: {
            shift_ = e.modifiers & kShiftMask;
            switch (e.scancode) {
                case kKeySpace: playback_.toggle(); break;
                case kKeyRight: playback_.step(+1); break;
                case kKeyLeft: playback_.step(-1); break;
                case kKeyEquals:
                case kKeyKpPlus: playback_.faster(); break;
                case kKeyMinus:
                case kKeyKpMinus: playback_.slower(); break;
                case kKeyHome: playback_.seekSample(0); break;
                case kKeyEnd: playback_.seekSample(model_.sampleCount() - 1); break;
                case kKeyI: playback_.setInterpolate(!playback_.interpolate()); break;
                case kKeyL: playback_.setLoop(!playback_.loop()); break;
                case kKeyH: visible_ = !visible_; break;
                case kKeyR: actions.resetView = true; break;
                case kKeyF12: actions.screenshot = true; break;
                case kKeyEscape:
                case kKeyQ: actions.quit = true; break;
                default: break;
            }
            break;
        }
        case STRATA_EVENT_KEY_UP: shift_ = e.modifiers & kShiftMask; break;
        case STRATA_EVENT_MOUSE_BUTTON_DOWN: {
            mx_ = e.x;
            my_ = e.y;
            if (visible_ && e.button == 1 && play_.contains(e.x, e.y)) {
                playback_.toggle();
            } else if (visible_ && e.button == 1 && speed_.contains(e.x, e.y)) {
                playback_.faster();
            } else if (visible_ && e.button == 1 && track_.contains(e.x, e.y - 0)) {
                dragScrub_ = true;
                scrubTo(e.x);
            } else if (visible_ && bar_.contains(e.x, e.y)) {
                // a click on the bar's background does nothing
            } else if (e.button == 3 || (e.button == 1 && shift_)) {
                dragPan_ = true;
            } else if (e.button == 1) {
                dragOrbit_ = true;
            }
            break;
        }
        case STRATA_EVENT_MOUSE_BUTTON_UP: dragOrbit_ = dragPan_ = dragScrub_ = false; break;
        case STRATA_EVENT_MOUSE_MOTION: {
            const float dx = e.x - mx_, dy = e.y - my_;
            mx_ = e.x;
            my_ = e.y;
            if (dragScrub_) scrubTo(e.x);
            else if (dragPan_) camera_.pan(dx, dy);
            else if (dragOrbit_) camera_.orbit(dx, dy);
            break;
        }
        case STRATA_EVENT_MOUSE_WHEEL: camera_.zoom(std::exp(-e.y * 0.1)); break;
        default: break;
    }
}

void NativeUi::draw(StrataEngine* engine, int width, int height) {
    layout(width, height);
    if (!visible_) return;
    const float px = height >= 600 ? 16.0f : 8.0f;  // multiples of 8 stay crisp
    const float line = px * 1.375f;
    std::vector<StrataRect> rects;
    std::vector<StrataText> texts;
    std::vector<std::string> strings;  // kept alive until submitted
    strings.reserve(64);
    auto text = [&](float x, float y, const std::string& s, const float c[4], float size) {
        strings.push_back(ascii(s));
        StrataText t{};
        t.x = x;
        t.y = y;
        t.pixel_size = size;
        t.flags = 0;
        for (int i = 0; i < 4; ++i) t.color[i] = c[i];
        texts.push_back(t);
    };

    const int k = playback_.sample();
    const size_t chars = size_t(std::max(12.0f, std::min(px >= 16 ? 34.0f : 44.0f, (float(width) - 48) / px)));
    const float pw = float(chars) * px + 24;

    // ---- panel ----
    float y = 12 + 12;
    const float x = 12 + 12;
    text(x, y, fit(model_.title, chars), kInk, px);
    y += line * 1.2f;
    const std::string sim = model_.metaValue("sim"), ver = model_.metaValue("simVersion"), seed = model_.metaValue("seed");
    text(x, y, fit((sim.empty() ? "sim" : sim) + " " + ver + "  seed " + (seed.empty() ? "?" : seed), chars), kMuted, px);
    y += line;
    text(x, y, fit(thousands(model_.sampleCount()) + " samples" +
                       (model_.keptEvery > 1 ? " (1 in " + std::to_string(model_.keptEvery) + ")" : std::string()),
                   chars),
         kMuted, px);
    y += line * 1.3f;
    // Stats, two to a row when there is room.
    const size_t col = chars / 2;
    for (size_t i = 0; i < model_.stats.size(); ++i) {
        const Stat& st = model_.stats[i];
        const auto& v = size_t(k) < st.values.size() ? st.values[size_t(k)] : std::nullopt;
        const std::string val = v ? statText(*v) : "-";
        const std::string name = fit(st.name, col > val.size() + 2 ? col - val.size() - 2 : 1);
        const float cx = x + float(i % 2) * float(col) * px;
        text(cx, y, name, kMuted, px);
        text(cx + float(col - 1 - val.size()) * px, y, val, kInk, px);
        if (i % 2 == 1 || i + 1 == model_.stats.size()) y += line;
    }
    if (!model_.stats.empty()) y += line * 0.3f;
    // Legend: a swatch in each set's colour (vertex-coloured sets get a three-colour swatch).
    float lx = x;
    for (const SetDef& s : model_.sets) {
        const std::string label = fit(s.label, chars - 2);
        const float w = px * float(label.size() + 2) + px;
        if (lx + w > x + float(chars) * px && lx > x) {
            lx = x;
            y += line;
        }
        if (s.vertexColor) {
            const float r[4] = {0.9f, 0.4f, 0.4f, 1}, g[4] = {0.4f, 0.8f, 0.4f, 1}, b[4] = {0.41f, 0.6f, 0.93f, 1};
            rects.push_back(rect(lx, y, px / 3, px, r));
            rects.push_back(rect(lx + px / 3, y, px / 3, px, g));
            rects.push_back(rect(lx + 2 * px / 3, y, px / 3, px, b));
        } else {
            const float c[4] = {s.color[0], s.color[1], s.color[2], 1};
            rects.push_back(rect(lx, y, px, px, c));
        }
        text(lx + px * 1.5f, y, label, kInk, px);
        lx += w;
    }
    if (!model_.sets.empty()) y += line * 1.3f;
    const std::string hash = size_t(k) < model_.hashes.size() ? model_.hashes[size_t(k)] : "";
    text(x, y, fit("tick " + thousands(playback_.tick()), chars), kMuted, px);
    y += line;
    text(x, y, fit("state " + hash, chars), kMuted, px);
    y += line;
    panel_ = {12, 12, pw, y - 12 + 8};
    rects.insert(rects.begin(), rect(panel_.x, panel_.y, panel_.w, panel_.h, kPanel));

    // ---- bar ----
    rects.push_back(rect(bar_.x, bar_.y, bar_.w, bar_.h, kPanel));
    rects.push_back(rect(play_.x, play_.y, play_.w, play_.h, kChip));
    const std::string playLabel = playback_.playing() ? "Pause" : (playback_.waiting() ? "Wait" : "Play");
    text(play_.x + (play_.w - px * float(playLabel.size())) / 2, play_.y + (play_.h - px) / 2, playLabel, kInk, px);
    rects.push_back(rect(speed_.x, speed_.y, speed_.w, speed_.h, kChip));
    char spd[16];
    std::snprintf(spd, sizeof spd, "%gx", playback_.speed());
    text(speed_.x + (speed_.w - px * float(std::string(spd).size())) / 2, speed_.y + (speed_.h - px) / 2, spd, kInk, px);
    // The scrubber: a track, the loaded part, the played part and a handle.
    const int n = model_.sampleCount();
    const float ty = track_.y + track_.h / 2 - 3;
    rects.push_back(rect(track_.x, ty, track_.w, 6, kTrack));
    int loaded = 0;
    while (loaded < n && model_.isLoaded(loaded)) ++loaded;
    if (n > 1 && loaded < n) {
        const float lc[4] = {0.45f, 0.5f, 0.56f, 1};
        rects.push_back(rect(track_.x, ty, track_.w * float(loaded) / float(n - 1), 6, lc));
    }
    const float f = n > 1 ? float(playback_.head() / double(n - 1)) : 0.0f;
    rects.push_back(rect(track_.x, ty, track_.w * f, 6, kAccent));
    rects.push_back(rect(track_.x + track_.w * f - 6, track_.y + track_.h / 2 - 10, 12, 20, kInk));
    if (width >= 900) {
        const std::string t = "tick " + thousands(playback_.tick()) + " / " + thousands(n ? model_.ticks.back() : 0);
        text(speed_.x - 16 - px * float(t.size()), speed_.y + (speed_.h - px) / 2, t, kMuted, px);
    }

    for (size_t i = 0; i < texts.size(); ++i) texts[i].utf8 = strings[i].c_str();
    check(strata_submit_rects(engine, rects.data(), int32_t(rects.size())), "strata_submit_rects");
    check(strata_submit_text(engine, texts.data(), int32_t(texts.size())), "strata_submit_text");
}

}  // namespace rp
