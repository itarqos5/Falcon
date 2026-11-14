#pragma once

#include "falcon/gfx/gl.hpp"

#include <array>
#include <cstddef>

namespace falcon {

// Keeps the CPU from running far ahead of the graphics card. Without vsync the swap does not wait, so nothing stops the game from queueing
// frames faster than the card draws them, and the driver's queue (often three frames or more) then fills and drains in bursts: the frames come out
// unevenly spaced, which looks choppy even at hundreds of frames a second, and what is on screen is older than it needs to be. A fence after each
// frame and a wait for the one `frames` back keeps the queue short and the spacing even; it costs nothing when the card is the faster one.
class FrameQueue {
public:
    FrameQueue() = default;
    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;
    ~FrameQueue() { drop(); }

    // Called after the swap of a frame. `frames` is how many frames may still be waiting on the card (1 or 2); 0 turns the limit off.
    void limit(int frames) {
        if (frames <= 0) {
            drop();
            return;
        }
        frames = frames > static_cast<int>(fences_.size()) ? static_cast<int>(fences_.size()) : frames;
        // The oldest fences first: each slot holds the fence of one frame, the newest at `next_`.
        if (fences_[next_] != nullptr) {  // (the frame that used this slot `size` frames ago is certainly done; the loop below waits for the ones in between)
            gl::DeleteSync(fences_[next_]);
            fences_[next_] = nullptr;
        }
        fences_[next_] = gl::FenceSync(gl::SYNC_GPU_COMMANDS_COMPLETE, 0);
        const std::size_t size = fences_.size();
        const std::size_t waitFor = (next_ + size - static_cast<std::size_t>(frames)) % size;  // the frame `frames` back
        next_ = (next_ + 1) % size;
        if (fences_[waitFor] != nullptr) {
            gl::ClientWaitSync(fences_[waitFor], gl::SYNC_FLUSH_COMMANDS_BIT, 100'000'000ull);  // (at most 0.1 s: a stuck card must not hang the game)
        }
    }

private:
    void drop() {
        for (gl::GLsync& f : fences_) {
            if (f != nullptr) gl::DeleteSync(f);
            f = nullptr;
        }
    }
    std::array<gl::GLsync, 4> fences_{};
    std::size_t next_ = 0;
};

}  // namespace falcon
