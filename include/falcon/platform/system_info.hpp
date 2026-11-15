#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace falcon {

// What the PC is made of, for the debug screen (F3, right-hand side). The CPU, the operating system and the amount of memory never change while
// the game runs, so they are read once.
struct SystemInfo {
    std::string cpu;                    // "AMD Ryzen 5 5600G" (see cleanCpuName); empty if unknown
    int logicalProcessors = 0;          // hardware threads
    std::string os;                     // "Windows 10 (build 19045)"
    std::uint64_t totalMemory = 0;      // bytes of RAM in the PC
};
[[nodiscard]] const SystemInfo& systemInfo();

// How much of that RAM is in use right now (read from the OS on every call, which is cheap but not for every frame).
struct SystemMemory {
    std::uint64_t total = 0;
    std::uint64_t available = 0;
};
[[nodiscard]] SystemMemory systemMemory();

// A graphics card as the OS lists it (for the memory the OpenGL strings do not give).
struct GpuAdapter {
    std::string name;
    std::uint64_t dedicatedBytes = 0;   // its own video memory
};
// The card whose name goes with the OpenGL renderer string (matched by name, case-insensitively); empty if none does.
[[nodiscard]] std::optional<GpuAdapter> findGpuAdapter(const std::string& glRenderer);

// A CPU's brand string without the marketing: "AMD Ryzen 5 5600G with Radeon Graphics" -> "AMD Ryzen 5 5600G";
// "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz" -> "Intel Core i7-9700K"; "AMD Ryzen 5 3600 6-Core Processor" -> "AMD Ryzen 5 3600".
[[nodiscard]] std::string cleanCpuName(std::string name);

// "31.9 GB" or "512 MB": bytes in the unit people read them in.
[[nodiscard]] std::string formatByteSize(std::uint64_t bytes);

// Lets the calling thread run only on time the frame thread and the other programs on the PC leave over (Windows: below-normal priority; elsewhere it does nothing). For the
// worker threads that make chunks: with it there can be many of them while the world loads without taking the frame thread's cores.
void lowerCurrentThreadPriority();

}  // namespace falcon
