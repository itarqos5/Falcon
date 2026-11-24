#pragma once

// How the last few hundred frames were spent, for the Developer page of the video settings: the average frame rate, the 1% and 0.1% lows (the frames that make a
// game feel slow even when the average says it is fast), and which of the frame's few tasks takes the most of it. The main loop records one sample a frame
// (client.cpp); the screen only reads them, so the numbers cost nothing while the panel is closed.

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace falcon {

// The four stretches a frame is cut into, in the order they happen. Presenting is whatever is left of the frame: waiting for the display (vsync) or for the
// frame rate cap, which is where the time of a frame that is fast enough goes, and all of a capped frame's. A frame's total is measured from one frame's start to
// the next's, so it is what the player feels.
enum FramePhase : int { kFrameUpdate = 0, kFrameDraw = 1, kFrameGui = 2, kFramePresent = 3, kFramePhaseCount = 4 };

inline constexpr const char* kFramePhaseNames[kFramePhaseCount] = {"World update (chunks, LODs, ticks)", "World draw (sky, chunks, LODs, entities)", "HUD & menus",
                                                                   "Waiting for the display"};

using FramePhaseTimes = std::array<double, kFramePhaseCount>;

class FrameStats {
public:
    // About ten seconds of a 60 fps game: enough for the 0.1% low to mean something, small enough to sort twice a frame while the panel is open.
    static constexpr int kCapacity = 600;

    // `totalMs`: the frame, start to start. `phases` is how much of it the main loop saw go to each task (presenting need not be filled in).
    void record(double totalMs, const FramePhaseTimes& phases) {
        ms_[static_cast<std::size_t>(next_)] = static_cast<float>(totalMs);
        auto& row = phases_[static_cast<std::size_t>(next_)];
        for (int i = 0; i < kFramePhaseCount; ++i) row[static_cast<std::size_t>(i)] = static_cast<float>(phases[static_cast<std::size_t>(i)]);
        next_ = (next_ + 1) % kCapacity;
        if (count_ < kCapacity) ++count_;
    }
    void reset() {
        next_ = 0;
        count_ = 0;
    }
    [[nodiscard]] int count() const { return count_; }

    [[nodiscard]] double averageMs() const {
        if (count_ == 0) return 0.0;
        double sum = 0.0;
        for (int i = 0; i < count_; ++i) sum += ms_[static_cast<std::size_t>(i)];
        return sum / static_cast<double>(count_);
    }
    [[nodiscard]] double averageFps() const {
        const double ms = averageMs();
        return ms > 0.0 ? 1000.0 / ms : 0.0;
    }
    // The frame time of the `worst` fraction of the frames (0.01: the slowest 1%, which is what a stutter is felt as). Sorted, so it costs a sort of the
    // samples; the screen asks for it while the panel is open and nowhere else.
    [[nodiscard]] double percentileMs(double worst) const {
        if (count_ == 0) return 0.0;
        std::vector<float> sorted(ms_.begin(), ms_.begin() + count_);
        std::sort(sorted.begin(), sorted.end());
        const double fraction = std::clamp(worst, 0.0, 1.0);
        const auto index = std::min(static_cast<std::size_t>(count_ - 1), static_cast<std::size_t>(fraction * static_cast<double>(count_)));
        return static_cast<double>(sorted[index]);
    }
    [[nodiscard]] double percentileFps(double worst) const {
        const double ms = percentileMs(worst);
        return ms > 0.0 ? 1000.0 / ms : 0.0;
    }
    // What each task took of an average frame, in milliseconds (presenting included: it is the rest of the frame).
    [[nodiscard]] FramePhaseTimes averagePhases() const {
        FramePhaseTimes out{};
        if (count_ == 0) return out;
        for (int i = 0; i < count_; ++i) {
            const auto& row = phases_[static_cast<std::size_t>(i)];
            for (int p = 0; p < kFramePhaseCount; ++p) out[static_cast<std::size_t>(p)] += static_cast<double>(row[static_cast<std::size_t>(p)]);
        }
        for (int p = 0; p < kFramePhaseCount; ++p) out[static_cast<std::size_t>(p)] /= static_cast<double>(count_);
        return out;
    }
    // Which task takes the most of an average frame (an index into kFramePhaseNames; kFramePresent when nothing was recorded).
    [[nodiscard]] int heaviestPhase() const {
        const FramePhaseTimes phases = averagePhases();
        int best = 0;
        for (int p = 1; p < kFramePhaseCount; ++p) {
            if (phases[static_cast<std::size_t>(p)] > phases[static_cast<std::size_t>(best)]) best = p;
        }
        return best;
    }

private:
    std::array<float, kCapacity> ms_{};
    std::array<std::array<float, kFramePhaseCount>, kCapacity> phases_{};
    int next_ = 0;
    int count_ = 0;
};

}  // namespace falcon
