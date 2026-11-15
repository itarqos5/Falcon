#include "falcon/platform/memory.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <malloc.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace falcon {

std::size_t processWorkingSetBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return counters.WorkingSetSize;
    }
#else
    // /proc/self/statm: total pages, then the resident ones (what Task Manager calls the working set).
    std::ifstream statm("/proc/self/statm");
    unsigned long long total = 0, resident = 0;
    if (statm >> total >> resident) return static_cast<std::size_t>(resident) * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
    return 0;
}

void trimWorkingSet() {
#ifdef _WIN32
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
#else
    malloc_trim(0);  // gives the free pages at the top of the heap, and whole free blocks inside it, back to the system
#endif
}

double millisSinceProcessStart() {
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == 0) return 0.0;
    GetSystemTimeAsFileTime(&now);
    const auto toTicks = [](const FILETIME& f) {
        return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;  // 100 ns units
    };
    return static_cast<double>(toTicks(now) - toTicks(created)) / 10000.0;
#else
    // /proc/self/stat field 22 is the start time in clock ticks after the boot; /proc/uptime is the time since the boot.
    std::ifstream stat("/proc/self/stat");
    std::string line;
    if (!std::getline(stat, line)) return 0.0;
    const std::size_t close = line.rfind(')');  // (the name in field 2 may hold spaces: count from after it)
    if (close == std::string::npos) return 0.0;
    std::istringstream rest(line.substr(close + 1));
    std::string field;
    unsigned long long startTicks = 0;
    for (int index = 3; index <= 22 && (rest >> field); ++index) {
        if (index == 22) startTicks = std::stoull(field);
    }
    std::ifstream uptimeFile("/proc/uptime");
    double uptime = 0.0;
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (!(uptimeFile >> uptime) || ticksPerSecond <= 0 || startTicks == 0) return 0.0;
    const double started = static_cast<double>(startTicks) / static_cast<double>(ticksPerSecond);
    return uptime > started ? (uptime - started) * 1000.0 : 0.0;
#endif
}

}  // namespace falcon
