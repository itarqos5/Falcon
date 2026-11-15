#include "falcon/platform/sampling_profiler.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace falcon::profiler {

#ifdef _WIN32

namespace {

const char* outputPath() {
    static const char* path = std::getenv("FALCON_SAMPLE");
    return path;
}

struct State {
    std::mutex mutex;
    std::vector<std::pair<HANDLE, std::string>> handles;  // a thread and its name
    std::atomic<bool> running{false};
    std::vector<std::pair<std::uint64_t, std::uint32_t>> samples;  // (instruction pointer, index of the thread in handles); only the sampling thread writes, and only while running
    std::thread sampling;
};

State& state() {
    static State s;
    return s;
}

// "exe <offset>" for the program itself, "dll <name>" for any other module (a graphics driver, the C runtime), "?" for anything else.
std::string describe(std::uint64_t rip, std::uint64_t exeBase) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(rip), &module) || module == nullptr) return "?";
    if (reinterpret_cast<std::uint64_t>(module) == exeBase) {
        char text[32];
        std::snprintf(text, sizeof(text), "exe %llx", static_cast<unsigned long long>(rip - exeBase));
        return text;
    }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    std::string name = path;
    const std::size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    return "dll " + name;
}

}  // namespace

bool enabled() { return outputPath() != nullptr; }

void registerThread(const char* name) {
    if (!enabled()) return;
    HANDLE h = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) return;
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.handles.emplace_back(h, name);
}

void start() {
    if (!enabled()) return;
    State& s = state();
    if (s.running.exchange(true)) return;
    s.samples.clear();
    s.sampling = std::thread([&s] {
        LARGE_INTEGER freq, next, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&next);
        while (s.running) {
            std::vector<HANDLE> copy;
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                for (const auto& [handle, name] : s.handles) copy.push_back(handle);
            }
            for (std::size_t i = 0; i < copy.size(); ++i) {
                const HANDLE h = copy[i];
                if (SuspendThread(h) == static_cast<DWORD>(-1)) continue;  // (a thread that has ended)
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(h, &ctx)) s.samples.emplace_back(static_cast<std::uint64_t>(ctx.Rip), static_cast<std::uint32_t>(i));
                ResumeThread(h);
            }
            next.QuadPart += freq.QuadPart / 4000;
            do {
                QueryPerformanceCounter(&now);
                YieldProcessor();
            } while (now.QuadPart < next.QuadPart && s.running);
        }
    });
}

void finish() {
    if (!enabled()) return;
    State& s = state();
    if (!s.running.exchange(false)) return;
    s.sampling.join();
    const std::uint64_t exeBase = reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));
    std::FILE* f = std::fopen(outputPath(), "w");
    if (f == nullptr) return;
    std::unordered_map<std::uint64_t, std::string> cache;  // the same few addresses come up again and again
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        for (const auto& [handle, name] : s.handles) names.push_back(name);
    }
    for (const auto& [rip, thread] : s.samples) {
        auto it = cache.find(rip);
        if (it == cache.end()) it = cache.emplace(rip, describe(rip, exeBase)).first;
        std::fprintf(f, "%s %s\n", thread < names.size() ? names[thread].c_str() : "thread", it->second.c_str());
    }
    std::fclose(f);
    std::fprintf(stderr, "[falcon] %zu profiler samples written to %s\n", s.samples.size(), outputPath());
    s.samples.clear();
}

#else

bool enabled() { return false; }
void registerThread(const char*) {}
void start() {}
void finish() {}

#endif

}  // namespace falcon::profiler
