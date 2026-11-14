#include "falcon/platform/file_lock.hpp"

#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace falcon {

LockProbe probeSessionLock(const std::filesystem::path& lockFile) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(lockFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return LockProbe::Missing;
    OVERLAPPED range{};  // offset 0; the length below covers the whole file, like Java's tryLock()
    const BOOL got = LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &range);
    if (got != 0) {
        UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &range);
        CloseHandle(handle);
        return LockProbe::Free;
    }
    const DWORD error = GetLastError();
    CloseHandle(handle);
    return error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING ? LockProbe::Locked : LockProbe::Free;
#else
    std::error_code ec;
    return std::filesystem::is_regular_file(lockFile, ec) ? LockProbe::Free : LockProbe::Missing;
#endif
}

}  // namespace falcon
