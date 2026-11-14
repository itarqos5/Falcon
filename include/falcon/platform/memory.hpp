#pragma once

#include <cstddef>

namespace falcon {

// Memory this process currently holds in RAM, in bytes (Windows working set); 0 if unknown.
[[nodiscard]] std::size_t processWorkingSetBytes();

// Hands back to the OS the pages this process is not actively using (memory that was freed but is
// still counted as resident, startup scratch space, rarely touched code). It lowers the resident
// size at once; anything used again is brought back on demand. Best called when loading is done.
void trimWorkingSet();

// Milliseconds since this process was created by the OS (so it includes loading the exe and the
// C runtime, before main() runs); 0 if unknown.
[[nodiscard]] double millisSinceProcessStart();

}  // namespace falcon
