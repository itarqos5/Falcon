#include "falcon/platform/file_lock.hpp"

#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
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
    // Java's FileChannel.tryLock() on Linux takes an exclusive POSIX (fcntl) lock on the whole file: ask whether somebody holds one.
    const int descriptor = ::open(lockFile.c_str(), O_RDWR | O_CLOEXEC);
    if (descriptor < 0) {
        std::error_code ec;
        return std::filesystem::is_regular_file(lockFile, ec) ? LockProbe::Free : LockProbe::Missing;  // (a file that cannot be opened for writing is not one Java locked)
    }
    struct flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;  // to the end of the file, whatever it grows to
    const bool asked = fcntl(descriptor, F_GETLK, &lock) == 0;
    ::close(descriptor);
    return asked && lock.l_type != F_UNLCK ? LockProbe::Locked : LockProbe::Free;
#endif
}

}  // namespace falcon
