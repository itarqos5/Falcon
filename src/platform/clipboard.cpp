#include "falcon/platform/clipboard.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace falcon {

std::string clipboardText() {
    if (!OpenClipboard(nullptr)) return {};
    std::string out;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(handle))) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 1) {
                out.resize(static_cast<std::size_t>(bytes - 1));
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), bytes, nullptr, nullptr);
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return out;
}

void setClipboardText(const std::string& utf8) {
    const int wideCount = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);  // (with the end mark)
    if (wideCount <= 0 || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideCount) * sizeof(wchar_t))) {
        if (wchar_t* wide = static_cast<wchar_t*>(GlobalLock(memory))) {
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide, wideCount);
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) GlobalFree(memory);  // (the system owns it once it was taken)
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

}  // namespace falcon
