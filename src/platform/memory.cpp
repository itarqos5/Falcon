#include "apex/platform/memory.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace apex {

std::size_t processWorkingSetBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return counters.WorkingSetSize;
    }
#endif
    return 0;
}

void trimWorkingSet() {
#ifdef _WIN32
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
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
    return 0.0;
#endif
}

}  // namespace apex
