#include "falcon/platform/http.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

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
                if (status == 429 || status == 503) {  // (Retry-After in seconds, when it is given as a number)
                    wchar_t value[32] = {};
                    DWORD valueSize = sizeof(value) - sizeof(wchar_t);
                    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Retry-After", value, &valueSize, WINHTTP_NO_HEADER_INDEX) != FALSE) answer.retryAfterSeconds = std::max(0, _wtoi(value));
                }
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

HttpAnswer httpsRequestPatient(const std::string& method, const std::string& host, const std::string& path, const std::string& bearer, const std::string& jsonBody, int maxWaitSeconds) {
    HttpAnswer answer = httpsRequest(method, host, path, bearer, jsonBody);
    int waited = 0, pause = 1;
    while ((answer.status == 429 || answer.status == 502 || answer.status == 503) && waited < maxWaitSeconds) {
        const int sleepFor = answer.retryAfterSeconds > 0 ? answer.retryAfterSeconds : pause;
        if (waited + sleepFor > maxWaitSeconds) break;  // (told to wait longer than is worth it here: the player is told instead)
        std::this_thread::sleep_for(std::chrono::seconds(sleepFor));
        waited += sleepFor;
        pause = std::min(pause * 2, 4);
        answer = httpsRequest(method, host, path, bearer, jsonBody);
    }
    return answer;
}

std::string busyAnswerWords(const HttpAnswer& answer) {
    std::string words = "Minecraft's servers are limiting requests from this account or connection right now (HTTP " + std::to_string(answer.status) + ")";
    if (answer.retryAfterSeconds > 0) words += "; they ask to wait " + std::to_string(answer.retryAfterSeconds) + " seconds";
    else if (answer.status == 429) words += "; wait about a minute";
    return words + ", then try again";
}

}  // namespace falcon
