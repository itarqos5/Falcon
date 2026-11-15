#include "falcon/platform/system_info.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace falcon {

namespace {

[[maybe_unused]] std::string lower(std::string s) {  // (the video memory lookup and the ARM CPU name use it; neither exists on every system)
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void eraseAll(std::string& s, const std::string& what) {
    for (std::size_t at = s.find(what); at != std::string::npos; at = s.find(what)) s.erase(at, what.size());
}

std::string cpuBrand() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int regs[4] = {};
    if (__get_cpuid_max(0x80000000u, nullptr) < 0x80000004u) return {};
    char text[49] = {};
    for (unsigned int i = 0; i < 3; ++i) {
        __get_cpuid(0x80000002u + i, &regs[0], &regs[1], &regs[2], &regs[3]);
        std::copy(reinterpret_cast<const char*>(regs), reinterpret_cast<const char*>(regs) + 16, text + i * 16);
    }
    return text;
#else
    // (not x86: the kernel's list has the name, on ARM as "Model name" or "Hardware")
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = lower(line.substr(0, colon));
        if (key.rfind("model name", 0) == 0 || key.rfind("hardware", 0) == 0) return line.substr(colon + 1);
    }
    return {};
#endif
}

std::string operatingSystem() {
#ifdef _WIN32
    // (GetVersionEx tells every program without a manifest that this is Windows 8; the kernel knows better.)
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            OSVERSIONINFOW v{};
            v.dwOSVersionInfoSize = sizeof(v);
            if (fn(&v) == 0) {
                const char* name = v.dwMajorVersion >= 10 ? (v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10") : "Windows";
                return std::string(name) + " (build " + std::to_string(v.dwBuildNumber) + ")";
            }
        }
    }
    return "Windows";
#else
    // /etc/os-release: PRETTY_NAME="Ubuntu 24.04.1 LTS"
    for (const char* file : {"/etc/os-release", "/usr/lib/os-release"}) {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
            std::string name = line.substr(12);
            if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) name = name.substr(1, name.size() - 2);
            if (!name.empty()) return name;
        }
    }
    return "Linux";
#endif
}

#ifdef _WIN32
std::string narrow(const wchar_t* text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    return out;
}
#endif

}  // namespace

std::string cleanCpuName(std::string name) {
    if (const std::size_t at = name.find(" @ "); at != std::string::npos) name.erase(at);
    if (const std::size_t at = name.find(" with "); at != std::string::npos) name.erase(at);
    for (const char* junk : {"(R)", "(TM)", "(tm)", "(r)", " CPU", " Processor"}) eraseAll(name, junk);
    // "6-Core", "12-Core": the thread count is shown on its own.
    for (std::size_t at = name.find("-Core"); at != std::string::npos; at = name.find("-Core")) {
        std::size_t from = at;
        while (from > 0 && std::isdigit(static_cast<unsigned char>(name[from - 1]))) --from;
        name.erase(from, at + 5 - from);
    }
    std::string out;  // one space between words, none at the ends
    bool space = false;
    for (const char c : name) {
        if (c == ' ' || c == '\t') {
            space = !out.empty();
        } else {
            if (space) out += ' ';
            space = false;
            out += c;
        }
    }
    return out;
}

std::string formatByteSize(std::uint64_t bytes) {
    char buffer[32];
    if (bytes >= (1ull << 30)) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / static_cast<double>(1ull << 30));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f MB", static_cast<double>(bytes) / static_cast<double>(1ull << 20));
    }
    return buffer;
}

const SystemInfo& systemInfo() {
    static const SystemInfo info = [] {
        SystemInfo s;
        s.cpu = cleanCpuName(cpuBrand());
        s.logicalProcessors = static_cast<int>(std::thread::hardware_concurrency());
        s.os = operatingSystem();
        s.totalMemory = systemMemory().total;
        return s;
    }();
    return info;
}

SystemMemory systemMemory() {
    SystemMemory m;
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != 0) {
        m.total = status.ullTotalPhys;
        m.available = status.ullAvailPhys;
    }
#else
    // /proc/meminfo, in kibibytes: "MemTotal:  16314424 kB"
    std::ifstream in("/proc/meminfo");
    std::string name;
    unsigned long long kib = 0;
    std::string unit;
    while (in >> name >> kib) {
        std::getline(in, unit);  // (the rest of the line: " kB")
        if (name == "MemTotal:") m.total = kib * 1024ull;
        else if (name == "MemAvailable:") m.available = kib * 1024ull;
    }
#endif
    return m;
}

std::optional<GpuAdapter> findGpuAdapter(const std::string& glRenderer) {
#ifdef _WIN32
    if (glRenderer.empty()) return std::nullopt;
    // (dxgi.dll is asked for at run time: it is on every Windows 7+ PC, and a PC without it just shows no video memory.)
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (dxgi == nullptr) return std::nullopt;
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = reinterpret_cast<CreateFactoryFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    static const GUID kFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    IDXGIFactory1* factory = nullptr;
    std::optional<GpuAdapter> found;
    if (create != nullptr && SUCCEEDED(create(kFactory1, reinterpret_cast<void**>(&factory))) && factory != nullptr) {
        const std::string renderer = lower(glRenderer);
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; !found && factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                const std::string name = narrow(desc.Description);
                const std::string lowered = lower(name);
                if (!lowered.empty() && (renderer.find(lowered) != std::string::npos || lowered.find(renderer) != std::string::npos)) {
                    found = GpuAdapter{name, static_cast<std::uint64_t>(desc.DedicatedVideoMemory)};
                }
            }
            adapter->Release();
        }
        factory->Release();
    }
    FreeLibrary(dxgi);
    return found;
#else
    (void)glRenderer;
    return std::nullopt;
#endif
}

}  // namespace falcon
