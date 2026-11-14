#include "apex/game/now_playing_toast.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace apex {
namespace {

constexpr int kPadding = 7;
constexpr int kNotesSize = 16;
constexpr int kNotesSpace = 30;
constexpr int kNoteFrames = 8;        // icon/music_notes is 16x128: eight 16x16 frames
constexpr double kNoteFrameMillis = 100.0;  // frametime 2 ticks
constexpr std::uint32_t kTextColor = 0xFFD3D3D3u;  // DyeColor.LIGHT_GRAY text colour

// Dye colours the notes cycle through (MUSIC_NOTE): each is the dye's RGB * 1.25, clamped; white
// is a fixed light grey.
constexpr std::array<std::uint32_t, 12> kDyes = {
    0xE6E6E6,  // white (hard-coded)
    0x9D9D97,  // light gray
    0x3AB3DA,  // light blue
    0x3C44AA,  // blue
    0x169C9C,  // cyan
    0x5E7C16,  // green
    0x80C71F,  // lime
    0xFED83D,  // yellow
    0xF9801D,  // orange
    0xF38BAA,  // pink
    0xB02E26,  // red
    0xC74EBD,  // magenta
};

std::uint32_t boosted(std::uint32_t rgb, bool isWhite) {
    if (isWhite) return rgb;
    const auto channel = [](std::uint32_t c) {
        return std::min<std::uint32_t>(255u, static_cast<std::uint32_t>(static_cast<float>(c) * 1.25f));
    };
    return (channel((rgb >> 16) & 0xFF) << 16) | (channel((rgb >> 8) & 0xFF) << 8) | channel(rgb & 0xFF);
}

}  // namespace

void NowPlayingToast::show(std::string title, double nowMillis) {
    title_ = std::move(title);
    startMillis_ = nowMillis;
    active_ = true;
}

double NowPlayingToast::visiblePortion(double nowMillis) const {
    if (!active_) return 0.0;
    const double elapsed = nowMillis - startMillis_;
    const auto ease = [](double t) {
        const double p = std::clamp(t / kSlideMillis, 0.0, 1.0);
        return p * p;
    };
    if (elapsed < kSlideMillis) return ease(elapsed);
    if (elapsed < kSlideMillis + kVisibleMillis) return 1.0;
    const double out = elapsed - kSlideMillis - kVisibleMillis;
    return out < kSlideMillis ? 1.0 - ease(out) : 0.0;
}

std::uint32_t NowPlayingToast::noteColor(double nowMillis) {
    const auto tick = static_cast<long long>(nowMillis / 25.0);
    const auto index = static_cast<std::size_t>((tick / 30) % static_cast<long long>(kDyes.size()));
    const std::size_t next = (index + 1) % kDyes.size();
    const float t = static_cast<float>(tick % 30) / 30.0f;

    const std::uint32_t a = boosted(kDyes[index], index == 0);
    const std::uint32_t b = boosted(kDyes[next], next == 0);
    const auto mix = [t](std::uint32_t from, std::uint32_t to) {
        return static_cast<std::uint32_t>(static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t);
    };
    return 0xFF000000u | (mix((a >> 16) & 0xFF, (b >> 16) & 0xFF) << 16) |
           (mix((a >> 8) & 0xFF, (b >> 8) & 0xFF) << 8) | mix(a & 0xFF, b & 0xFF);
}

void NowPlayingToast::render(GuiGraphics& graphics, double nowMillis) {
    const double portion = visiblePortion(nowMillis);
    if (!active_ || portion <= 0.0) {
        if (active_ && nowMillis - startMillis_ > 2.0 * kSlideMillis + kVisibleMillis) active_ = false;
        return;
    }

    const int width = kNotesSpace + graphics.textWidth(title_) + kPadding;
    const float x = static_cast<float>(width) * static_cast<float>(portion) - static_cast<float>(width);

    graphics.pushPose();
    graphics.pose().translate(x, 0.0f);
    graphics.blitSprite("toast/now_playing", 0, 0, width, kHeight);

    const int frame = static_cast<int>(nowMillis / kNoteFrameMillis) % kNoteFrames;
    graphics.blitSpriteFrame("icon/music_notes", kPadding, kPadding, kNotesSize, kNotesSize, frame,
                             kNoteFrames, noteColor(nowMillis));
    graphics.text(title_, static_cast<float>(kNotesSpace),
                  static_cast<float>(kHeight / 2 - Font::kLineHeight / 2), kTextColor);
    graphics.popPose();
}

}  // namespace apex
