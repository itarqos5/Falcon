#include "falcon/platform/http.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace falcon {

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // (ASCII: methods, tokens, host names, paths)

}  // namespace

HttpAnswer httpsRequest(const std::string& method, const std::string& host, const std::string& path, const std::string& bearer, const std::string& jsonBody) {
    HttpAnswer answer;
    HINTERNET session = WinHttpOpen(L"Falcon", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) session = WinHttpOpen(L"Falcon", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        answer.error = "WinHTTP cannot start (error " + std::to_string(GetLastError()) + ")";
        return answer;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);
    if (HINTERNET connection = WinHttpConnect(session, widen(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)) {
        if (HINTERNET request = WinHttpOpenRequest(connection, widen(method).c_str(), widen(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
            std::wstring headers = L"Accept: application/json\r\n";
            if (!bearer.empty()) headers += L"Authorization: Bearer " + widen(bearer) + L"\r\n";
            if (!jsonBody.empty()) headers += L"Content-Type: application/json\r\n";
            void* body = jsonBody.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(jsonBody.data());
            const DWORD bodySize = static_cast<DWORD>(jsonBody.size());
            if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()), body, bodySize, bodySize, 0) != FALSE && WinHttpReceiveResponse(request, nullptr) != FALSE) {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                answer.status = static_cast<int>(status);
                for (;;) {
                    DWORD available = 0;
                    if (WinHttpQueryDataAvailable(request, &available) == FALSE || available == 0) break;
                    std::string chunk(available, '\0');
                    DWORD read = 0;
                    if (WinHttpReadData(request, chunk.data(), available, &read) == FALSE || read == 0) break;
                    answer.body.append(chunk, 0, read);
                    if (answer.body.size() > (1u << 20)) break;  // (nothing these answers need is anywhere near a megabyte)
                }
            } else {
                answer.error = "error " + std::to_string(GetLastError());
            }
            WinHttpCloseHandle(request);
        } else {
            answer.error = "error " + std::to_string(GetLastError());
        }
        WinHttpCloseHandle(connection);
    } else {
        answer.error = "error " + std::to_string(GetLastError());
    }
    WinHttpCloseHandle(session);
    return answer;
}

#else

HttpAnswer httpsRequest(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) {
    HttpAnswer answer;
    answer.error = "no network code for this system";
    return answer;
}

#endif

}  // namespace falcon
