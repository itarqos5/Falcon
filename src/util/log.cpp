#include "falcon/util/log.hpp"

#include "falcon/platform/dirs.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/platform/system_info.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>

#ifdef __GNUC__
#include <cxxabi.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

namespace falcon::log {
namespace {

constexpr std::size_t kRecentLines = 400;

struct State {
    std::mutex mutex;
    std::FILE* file = nullptr;
    std::filesystem::path dir, current;
    Level fileLevel = Level::Info;
    bool echo = true;
    int keep = 15;
    std::deque<std::string> recent;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::string version, commandLine;
    std::map<std::string, std::function<std::string()>> context;
    std::function<void(const std::filesystem::path&)> notify;
    std::atomic<bool> crashing{false};
};

// (never destroyed: lines written while the program ends, or from a crash handler, still have somewhere to go)
State& state() {
    static State* s = new State();
    return *s;
}

int rank(Level level) { return static_cast<int>(level); }

const char* levelText(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO ";
        case Level::Warn: return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "INFO ";
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string nowText(const char* pattern, bool millis) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), pattern, &tm);
    std::string out = buffer;
    if (millis) {
        char ms[8];
        std::snprintf(ms, sizeof(ms), ".%03d", static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000));
        out += ms;
    }
    return out;
}

std::string vformat(const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (n < 0) return format;
    std::string s(static_cast<std::size_t>(n), '\0');
    std::vsnprintf(s.data(), static_cast<std::size_t>(n) + 1, format, args);
    return s;
}

double uptimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - state().started).count(); }

// One message to the file (a line for each line of the message), the recent-lines ring and, for Info and up, the console. `console` is what the console shows after "[falcon] ".
void emit(Level level, const std::string& category, const std::string& message, const std::string& console) {
    State& s = state();
    std::string text = message;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::size_t start = 0;
    do {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(start, end - start);
        if (!part.empty() && part.back() == '\r') part.pop_back();
        start = end + 1;
        const std::string fileLine = nowText("%Y-%m-%d %H:%M:%S", true) + " " + levelText(level) + " [" + category + "] " + part;
        s.recent.push_back(fileLine);
        if (s.recent.size() > kRecentLines) s.recent.pop_front();
        if (s.file != nullptr && rank(level) >= rank(s.fileLevel)) {
            std::fputs(fileLine.c_str(), s.file);
            std::fputc('\n', s.file);
            std::fflush(s.file);
        }
    } while (start <= text.size());
    if (s.echo && rank(level) >= rank(Level::Info)) {
        std::string shown = console;
        while (!shown.empty() && (shown.back() == '\n' || shown.back() == '\r')) shown.pop_back();
        std::printf("[falcon] %s\n", shown.c_str());
    }
}

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

Level inferLevel(const std::string& message) {
    const std::string m = lower(message);
    if (contains(m, "failed") || contains(m, "error") || contains(m, "fatal") || contains(m, "crash")) return Level::Error;
    if (contains(m, "could not") || contains(m, "cannot") || contains(m, "unreadable") || contains(m, "not found") || contains(m, "unavailable") || contains(m, "did not") ||
        contains(m, "not implemented") || contains(m, "missing") || contains(m, "warning"))
        return Level::Warn;
    return Level::Info;
}

// The category of an old console line, from how it starts: a table of the lines the game has always printed ("audio: muted" is [audio] "muted"; the rest keep their words and are [client]).
std::string inferCategory(std::string& message) {
    const std::string m = lower(message);
    static const std::pair<const char*, const char*> kStarts[] = {
        {"graphics api", "render"}, {"opengl", "render"}, {"vulkan", "render"}, {"window ", "render"}, {"world drawing", "render"}, {"mesh buffer", "render"},
        {"spawn found", "world"}, {"spawn area", "world"}, {"the spawn area", "world"}, {"playing world", "world"}, {"game mode", "world"}, {"travelling", "world"}, {"the nether", "world"},
        {"the end", "world"}, {"the ender dragon", "world"}, {"created world", "world"}, {"could not create the world", "world"}, {"cannot open world", "world"}, {"could not start the world", "world"},
        {"could not start the session", "world"}, {"could not read the saves", "world"}, {"could not delete world", "world"}, {"world preset", "world"}, {"the world generator", "world"},
        {"loaded the save", "save"}, {"saved the world", "save"}, {"could not save", "save"}, {"saved ", "capture"}, {"failed to save", "capture"},
        {"connecting", "net"}, {"joining", "net"}, {"left the server", "net"}, {"cannot join", "net"}, {"chat is not signed", "net"}, {"a chat message", "net"}, {"a command could not", "net"},
        {"the server's command", "net"}, {"chunk ", "net"},
        {"assets", "resources"}, {"resource packs", "resources"}, {"title badge", "resources"}, {"loaded ", "resources"}, {"block texture", "resources"}, {"reload", "resources"},
        {"sound ", "audio"}, {"music", "audio"}, {"audio", "audio"},
    };
    for (const auto& [start, category] : kStarts) {
        if (m.compare(0, std::strlen(start), start) == 0) {
            const std::string name = category;
            // "audio: muted" and "music: x" lose their word; the others keep the whole line
            if (name == "audio" && m.compare(0, 6, "audio:") == 0) message = message.substr(7);
            return name;
        }
    }
    return "client";
}

FILE* openForWriting(const std::filesystem::path& path) {
#ifdef _WIN32
    return _wfopen(path.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

bool endsCleanly(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return true;  // (nothing there to be unclean)
    const std::streamoff size = in.tellg();
    const std::streamoff back = std::min<std::streamoff>(size, 1024);
    in.seekg(size - back);
    std::string tail(static_cast<std::size_t>(back), '\0');
    in.read(tail.data(), back);
    return tail.find("end of log") != std::string::npos;
}

// The newest `keep` files named <prefix>*<suffix> stay.
void prune(const std::filesystem::path& dir, const std::string& prefix, const std::string& suffix, int keep) {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() > prefix.size() + suffix.size() && name.compare(0, prefix.size(), prefix) == 0 && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());  // (the date is in the name)
    for (std::size_t i = 0; i + static_cast<std::size_t>(std::max(keep, 1)) < found.size(); ++i) std::filesystem::remove(found[i], ec);
}

std::string compilerText() {
#if defined(__VERSION__)
    return __VERSION__;
#else
    return "unknown compiler";
#endif
}

std::string contextText() {
    State& s = state();
    std::vector<std::pair<std::string, std::function<std::string()>>> providers;
    {
        // (a try only: the crash may have happened with this lock held)
        if (s.mutex.try_lock()) {
            providers.assign(s.context.begin(), s.context.end());
            s.mutex.unlock();
        } else {
            providers.assign(s.context.begin(), s.context.end());
        }
    }
    std::string out;
    for (const auto& [name, provider] : providers) {
        std::string value;
        try {
            value = provider();
        } catch (...) {
            value = "(could not be read)";
        }
        out += "  " + name + ": " + value + "\n";
    }
    return out.empty() ? "  (nothing was registered)\n" : out;
}

// ---- the report -----------------------------------------------------------------------------------------------------------------------------------------------------

std::filesystem::path reportDirectory() {
    State& s = state();
    if (!s.dir.empty()) return s.dir;
    std::error_code ec;
    const auto fallback = std::filesystem::temp_directory_path(ec) / "falcon-logs";
    std::filesystem::create_directories(fallback, ec);
    return fallback;
}

std::filesystem::path writeReport(const std::string& what, const std::string& where, const std::vector<std::string>& stack, const std::string& extra) {
    State& s = state();
    const SystemInfo& machine = systemInfo();
    std::ostringstream out;
    out << "Falcon crash report\n===================\n";
    out << "When:     " << nowText("%Y-%m-%d %H:%M:%S", false) << "\n";
    out << "Program:  " << (s.version.empty() ? "Falcon" : s.version) << " (built " << __DATE__ << " " << __TIME__ << ", " << compilerText() << ")\n";
    out << "System:   " << machine.os << "; " << machine.cpu << " (" << machine.logicalProcessors << " threads); " << formatByteSize(machine.totalMemory) << " of memory\n";
    out << "What:     " << what << "\n";
    if (!where.empty()) out << "Where:    " << where << "\n";
    out << "Running:  " << static_cast<long>(uptimeSeconds()) << " s since the program started\n";
    {
        const SystemMemory memory = systemMemory();
        out << "Memory:   " << formatByteSize(processWorkingSetBytes()) << " held by Falcon; " << formatByteSize(memory.available) << " of " << formatByteSize(memory.total) << " free on the PC\n";
    }
    if (!s.commandLine.empty()) out << "Started:  " << s.commandLine << "\n";
    if (!extra.empty()) out << "\n" << extra << (extra.back() == '\n' ? "" : "\n");
    out << "\nStack (the call that failed first). To read an address: addr2line -f -C -e Falcon.exe <offset> on the build it came from.\n";
    if (stack.empty()) out << "  (not available)\n";
    for (std::size_t i = 0; i < stack.size(); ++i) out << "  #" << i << " " << stack[i] << "\n";
    out << "\nWhat the game was doing:\n" << contextText();
    out << "\nThe last lines of the log:\n";
    {
        std::vector<std::string> lines;
        if (s.mutex.try_lock()) {
            lines.assign(s.recent.begin(), s.recent.end());
            s.mutex.unlock();
        } else {
            lines.assign(s.recent.begin(), s.recent.end());
        }
        const std::size_t from = lines.size() > 80 ? lines.size() - 80 : 0;
        for (std::size_t i = from; i < lines.size(); ++i) out << "  " << lines[i] << "\n";
        if (lines.empty()) out << "  (none)\n";
    }
    const std::filesystem::path path = reportDirectory() / ("crash-" + nowText("%Y%m%d-%H%M%S", false) + ".log");
    std::FILE* f = openForWriting(path);
    if (f == nullptr) return {};
    const std::string text = out.str();
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return path;
}

// Everything a crash does after the stack is known: the report, a line in the log, a word to the console, and a window if one was asked for.
void finishCrash(const std::string& what, const std::string& where, const std::vector<std::string>& stack, const std::string& extra) {
    const std::filesystem::path report = writeReport(what, where, stack, extra);
    emit(Level::Error, "crash", "CRASH: " + what + (where.empty() ? "" : " at " + where) + (report.empty() ? "; the report could not be written" : "; report: " + report.string()),
         "crash: " + what + (where.empty() ? "" : " at " + where) + (report.empty() ? "" : " (report: " + report.string() + ")"));
    State& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.file != nullptr) std::fflush(s.file);
    }
    if (s.notify && !report.empty()) {
        try {
            s.notify(report);
        } catch (...) {
        }
    }
}

#ifdef _WIN32

const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_BREAKPOINT: return "breakpoint";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data";
        case EXCEPTION_FLT_DENORMAL_OPERAND: return "floating point: denormal operand";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "floating point: divide by zero";
        case EXCEPTION_FLT_INEXACT_RESULT: return "floating point: inexact result";
        case EXCEPTION_FLT_INVALID_OPERATION: return "floating point: invalid operation";
        case EXCEPTION_FLT_OVERFLOW: return "floating point: overflow";
        case EXCEPTION_FLT_STACK_CHECK: return "floating point: stack check";
        case EXCEPTION_FLT_UNDERFLOW: return "floating point: underflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "page could not be read in (a file or a drive gone?)";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_INT_OVERFLOW: return "integer overflow";
        case EXCEPTION_INVALID_DISPOSITION: return "invalid disposition";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "noncontinuable exception";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow (a call chain that never ends)";
        case 0xC0000017: return "out of memory";
        case 0xC0000374: return "heap corruption";
        case 0xC0000409: return "fast fail (stack buffer overrun or a failed check in the runtime)";
        case 0xE06D7363: return "uncaught C++ exception";
        default: return "unknown exception";
    }
}

struct DbgHelp {
    HMODULE module = nullptr;
    BOOL(WINAPI* symInitialize)(HANDLE, PCSTR, BOOL) = nullptr;
    DWORD(WINAPI* symSetOptions)(DWORD) = nullptr;
    BOOL(WINAPI* symFromAddr)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO) = nullptr;
    BOOL(WINAPI* stackWalk)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64,
                            PTRANSLATE_ADDRESS_ROUTINE64) = nullptr;
    PVOID(WINAPI* functionTable)(HANDLE, DWORD64) = nullptr;
    DWORD64(WINAPI* moduleBase)(HANDLE, DWORD64) = nullptr;
    bool ready = false;
};

DbgHelp& dbghelp() {
    static DbgHelp* d = [] {
        auto* h = new DbgHelp();
        h->module = LoadLibraryA("dbghelp.dll");
        if (h->module == nullptr) return h;
        const auto load = [&](auto& fn, const char* name) { fn = reinterpret_cast<std::decay_t<decltype(fn)>>(reinterpret_cast<void*>(GetProcAddress(h->module, name))); };
        load(h->symInitialize, "SymInitialize");
        load(h->symSetOptions, "SymSetOptions");
        load(h->symFromAddr, "SymFromAddr");
        load(h->stackWalk, "StackWalk64");
        load(h->functionTable, "SymFunctionTableAccess64");
        load(h->moduleBase, "SymGetModuleBase64");
        if (h->symInitialize != nullptr && h->symSetOptions != nullptr) {
            h->symSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            h->ready = h->symInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
        }
        return h;
    }();
    return *d;
}

// "Falcon.exe+0x1A2B3C (falcon::GameSession::die())"
std::string describeAddress(std::uintptr_t address) {
    char text[512];
    HMODULE module = nullptr;
    std::string place;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module) && module != nullptr) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = std::strrchr(path, '\\');
        std::snprintf(text, sizeof(text), "%s+0x%llX", name != nullptr ? name + 1 : path, static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
        place = text;
    } else {
        std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(address));
        place = text;
    }
    DbgHelp& d = dbghelp();
    if (d.ready && d.symFromAddr != nullptr) {
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (d.symFromAddr(GetCurrentProcess(), address, &displacement, symbol) != FALSE && symbol->NameLen > 0) {
            std::snprintf(text, sizeof(text), " (%s+0x%llX)", symbol->Name, static_cast<unsigned long long>(displacement));
            place += text;
        }
    }
    return place;
}

std::vector<std::string> stackFromContext(CONTEXT* original) {
    std::vector<std::string> out;
#if defined(_M_X64) || defined(__x86_64__)
    DbgHelp& d = dbghelp();
    if (d.stackWalk == nullptr) return out;
    CONTEXT context = *original;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 48; ++i) {
        if (!d.stackWalk(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &context, nullptr, d.functionTable, d.moduleBase, nullptr)) break;
        if (frame.AddrPC.Offset == 0) break;
        out.push_back(describeAddress(static_cast<std::uintptr_t>(frame.AddrPC.Offset)));
    }
#else
    (void)original;
#endif
    return out;
}

std::vector<std::string> stackHere() {
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(1, 48, frames, nullptr);
    std::vector<std::string> out;
    for (USHORT i = 0; i < n; ++i) out.push_back(describeAddress(reinterpret_cast<std::uintptr_t>(frames[i])));
    return out;
}

LONG WINAPI unhandledException(EXCEPTION_POINTERS* info) {
    if (state().crashing.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    std::string what = std::string(exceptionName(record.ExceptionCode));
    char code[32];
    std::snprintf(code, sizeof(code), " (0x%08lX)", static_cast<unsigned long>(record.ExceptionCode));
    what += code;
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        char detail[96];
        const ULONG_PTR kind = record.ExceptionInformation[0];
        std::snprintf(detail, sizeof(detail), ", %s address 0x%llX", kind == 0 ? "reading" : kind == 1 ? "writing" : "executing", static_cast<unsigned long long>(record.ExceptionInformation[1]));
        what += detail;
        if (record.ExceptionInformation[1] < 0x10000) what += " (a null pointer was used)";
    }
    const std::string where = describeAddress(reinterpret_cast<std::uintptr_t>(record.ExceptionAddress));
    char thread[64];
    std::snprintf(thread, sizeof(thread), "Thread:   id %lu\n", static_cast<unsigned long>(GetCurrentThreadId()));
    finishCrash(what, where, stackFromContext(info->ContextRecord), thread);
    return EXCEPTION_EXECUTE_HANDLER;
}

void signalHandler(int signal) {
    if (state().crashing.exchange(true)) std::_Exit(3);
    const char* name = signal == SIGABRT ? "abort() was called (a failed check, or the runtime gave up)" : signal == SIGSEGV ? "invalid memory access" : signal == SIGFPE ? "arithmetic error" : "illegal instruction";
    finishCrash(name, "", stackHere(), "");
    std::_Exit(3);
}

#else

void signalHandler(int signal) {
    if (state().crashing.exchange(true)) _exit(3);
    const char* name = signal == SIGABRT ? "abort() was called (a failed check, or the runtime gave up)" : signal == SIGSEGV ? "invalid memory access (segmentation fault)"
                       : signal == SIGBUS ? "bus error" : signal == SIGFPE ? "arithmetic error" : "illegal instruction";
    void* frames[48];
    const int n = backtrace(frames, 48);
    std::vector<std::string> stack;
    if (char** symbols = backtrace_symbols(frames, n)) {
        for (int i = 1; i < n; ++i) {
            std::string line = symbols[i];
            const std::size_t open = line.find('('), plus = line.find('+', open == std::string::npos ? 0 : open);
            if (open != std::string::npos && plus != std::string::npos && plus > open + 1) {
                int status = 0;
                const std::string mangled = line.substr(open + 1, plus - open - 1);
                if (char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status); demangled != nullptr && status == 0) line = line.substr(0, open + 1) + demangled + line.substr(plus);
                std::free(demangled);
            }
            stack.push_back(line);
        }
        std::free(symbols);
    }
    finishCrash(name, "", stack, "");
    _exit(3);
}

#endif

void terminateHandler() {
    if (state().crashing.exchange(true)) std::_Exit(3);
    std::string what = "std::terminate was called";
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            what = "uncaught C++ exception " + typeName(typeid(ex)) + ": " + ex.what();
        } catch (...) {
            what = "uncaught C++ exception of a type that is not std::exception";
        }
    }
#ifdef _WIN32
    finishCrash(what, "", stackHere(), "");
#else
    void* frames[48];
    const int n = backtrace(frames, 48);
    std::vector<std::string> stack;
    if (char** symbols = backtrace_symbols(frames, n)) {
        for (int i = 1; i < n; ++i) stack.push_back(symbols[i]);
        std::free(symbols);
    }
    finishCrash(what, "", stack, "");
#endif
    std::_Exit(3);
}

}  // namespace

// ---- the public side ------------------------------------------------------------------------------------------------------------------------------------------------

std::string typeName(const std::type_info& type) {
#ifdef __GNUC__
    int status = 0;
    char* demangled = abi::__cxa_demangle(type.name(), nullptr, nullptr, &status);
    std::string out = status == 0 && demangled != nullptr ? demangled : type.name();
    std::free(demangled);
    return out;
#else
    return type.name();
#endif
}

void write(Level level, const std::string& category, const std::string& message) { emit(level, category, message, category + ": " + message); }

void writef(Level level, const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string message = vformat(format, args);
    va_end(args);
    write(level, category, message);
}

void line(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string whole = vformat(format, args);
    va_end(args);
    std::string message = whole;
    const Level level = inferLevel(message);
    const std::string category = inferCategory(message);
    emit(level, category, message, whole);
}

bool init(const Settings& settings) {
    State& s = state();
    s.echo = settings.echo;
    s.keep = settings.keep;
    s.version = settings.version;
    s.commandLine = settings.commandLine;
    if (const char* level = std::getenv("FALCON_LOG_LEVEL"); level != nullptr && lower(level) == "debug") s.fileLevel = Level::Debug;

    std::filesystem::path dir;
    if (const char* over = std::getenv("FALCON_LOG_DIR"); over != nullptr && *over != '\0') {
        dir = over;
    } else if (settings.toFile) {
        if (const auto base = falconDataDir()) dir = *base / "logs";
    }
    bool opened = false;
    std::string previous;  // the log of the run before, when it did not end cleanly
    std::filesystem::file_time_type previousTime{};
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!ec) {
            const std::filesystem::path current = dir / "client.log";
            if (std::filesystem::exists(current, ec)) {
                const bool clean = endsCleanly(current);
                std::string stamp = nowText("%Y%m%d-%H%M%S", false);
                const auto modified = std::filesystem::last_write_time(current, ec);
                if (!ec) {
                    previousTime = modified;
                    const auto sysTime = std::chrono::clock_cast<std::chrono::system_clock>(modified);
                    const std::time_t t = std::chrono::system_clock::to_time_t(sysTime);
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    char buffer[32];
                    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &tm);
                    stamp = buffer;
                }
                std::filesystem::path target = dir / ("client-" + stamp + (clean ? "" : "-unclean") + ".log");
                for (int i = 2; std::filesystem::exists(target, ec) && i < 100; ++i) target = dir / ("client-" + stamp + "-" + std::to_string(i) + (clean ? "" : "-unclean") + ".log");
                std::filesystem::rename(current, target, ec);
                if (!clean) previous = target.filename().string();
            }
            prune(dir, "client-", ".log", settings.keep);
            prune(dir, "crash-", ".log", settings.keep);
            if (std::FILE* f = openForWriting(current)) {
                std::lock_guard<std::mutex> lock(s.mutex);
                s.file = f;
                s.dir = dir;
                s.current = current;
                opened = true;
            }
        }
    }

    // The header: what this is, and what it runs on.
    const SystemInfo& machine = systemInfo();
    write(Level::Info, "client", "---- " + (settings.version.empty() ? std::string("Falcon") : settings.version) + " ----");
    writef(Level::Info, "client", "started %s; built %s %s with %s", nowText("%Y-%m-%d %H:%M:%S", false).c_str(), __DATE__, __TIME__, compilerText().c_str());
#ifdef _WIN32
    writef(Level::Info, "system", "%s; process %lu", machine.os.c_str(), static_cast<unsigned long>(GetCurrentProcessId()));
#else
    writef(Level::Info, "system", "%s; process %d", machine.os.c_str(), static_cast<int>(getpid()));
#endif
    writef(Level::Info, "system", "%s, %d threads, %s of memory", machine.cpu.c_str(), machine.logicalProcessors, formatByteSize(machine.totalMemory).c_str());
    if (!settings.commandLine.empty()) writef(Level::Info, "client", "arguments: %s", settings.commandLine.c_str());
    if (opened) writef(Level::Info, "log", "writing %s", s.current.string().c_str());
    if (!previous.empty()) {
        writef(Level::Warn, "log", "the run before this one did not end cleanly (it crashed, or was closed from outside): its log is %s", previous.c_str());
        // A crash report written at about the time that log stopped says what happened.
        std::error_code ec;
        std::filesystem::path newest;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.compare(0, 6, "crash-") == 0 && entry.path().extension() == ".log" && (newest.empty() || entry.path() > newest)) newest = entry.path();
        }
        if (!newest.empty() && std::filesystem::last_write_time(newest, ec) >= previousTime - std::chrono::seconds(10)) writef(Level::Warn, "log", "a crash report from then: %s", newest.string().c_str());
        else write(Level::Warn, "log", "there is no crash report for it (a crash would have written one: it was probably closed from outside, or the PC lost power)");
    }
    return opened;
}

void shutdown(int exitCode) {
    State& s = state();
    writef(Level::Info, "log", "end of log (exit code %d, ran %.1f s)", exitCode, uptimeSeconds());
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.file != nullptr) {
        std::fclose(s.file);
        s.file = nullptr;
    }
}

std::filesystem::path directory() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.dir;
}

std::filesystem::path currentFile() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.current;
}

void addContext(const std::string& name, std::function<std::string()> provider) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.context[name] = std::move(provider);
}

void removeContext(const std::string& name) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.context.erase(name);
}

std::vector<std::string> recentLines(std::size_t count) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    const std::size_t from = s.recent.size() > count ? s.recent.size() - count : 0;
    return {s.recent.begin() + static_cast<std::ptrdiff_t>(from), s.recent.end()};
}

void installCrashHandler(std::function<void(const std::filesystem::path&)> notify) {
    State& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.notify = std::move(notify);
    }
    std::set_terminate(terminateHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledException);  // (an access violation, a stack overflow, a division by zero ... with where and what)
    std::signal(SIGABRT, signalHandler);              // (abort(): the runtime's own failed checks; the others stay with the filter, which knows more)
#else
    struct sigaction action {};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = static_cast<int>(SA_RESETHAND);
    for (const int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(signal, &action, nullptr);
#endif
}

std::filesystem::path writeCrashReport(const std::string& title, const std::string& detail) {
    std::vector<std::string> stack;
#ifdef _WIN32
    stack = stackHere();
#else
    void* frames[48];
    const int n = backtrace(frames, 48);
    if (char** symbols = backtrace_symbols(frames, n)) {
        for (int i = 1; i < n; ++i) stack.push_back(symbols[i]);
        std::free(symbols);
    }
#endif
    const std::filesystem::path report = writeReport(detail.empty() ? title : title + ": " + detail, "", stack, "");
    emit(Level::Error, "crash", "FATAL: " + title + (detail.empty() ? "" : ": " + detail) + (report.empty() ? "" : "; report: " + report.string()), "fatal: " + title + (detail.empty() ? "" : ": " + detail));
    return report;
}

}  // namespace falcon::log
