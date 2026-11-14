#include "falcon/platform/ownership.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace falcon {

namespace {

std::string normalizedId(std::string id) {
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    for (char& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return id;
}

struct HttpAnswer {
    int status = 0;  // 0: no answer at all
    std::string body;
    std::string error;
};

#ifdef _WIN32
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // (ASCII: tokens, host names)

// One HTTPS GET with a bearer token (WinHTTP: the system's certificates and proxy settings).
HttpAnswer httpsGet(const std::string& host, const std::string& path, const std::string& bearer) {
    HttpAnswer answer;
    HINTERNET session = WinHttpOpen(L"Falcon", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) session = WinHttpOpen(L"Falcon", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        answer.error = "WinHTTP cannot start (error " + std::to_string(GetLastError()) + ")";
        return answer;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);
    if (HINTERNET connection = WinHttpConnect(session, widen(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)) {
        if (HINTERNET request = WinHttpOpenRequest(connection, L"GET", widen(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
            const std::wstring headers = L"Authorization: Bearer " + widen(bearer) + L"\r\nAccept: application/json\r\n";
            if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE && WinHttpReceiveResponse(request, nullptr) != FALSE) {
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
                    if (answer.body.size() > (1u << 20)) break;  // (nothing these two answers need is anywhere near a megabyte)
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
HttpAnswer httpsGet(const std::string&, const std::string&, const std::string&) {
    HttpAnswer answer;
    answer.error = "no network code for this system";
    return answer;
}
#endif

}  // namespace

bool entitlementsListGame(const std::string& body) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array()) return false;
    for (const nlohmann::json& item : j["items"]) {
        if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) continue;
        const std::string name = item["name"].get<std::string>();
        if (name == "game_minecraft" || name == "product_minecraft") return true;
    }
    return false;
}

bool profileMatches(const std::string& body, const std::string& uuid, const std::string& name, std::string& why) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string()) {
        why = "Minecraft sent no profile for this account";
        return false;
    }
    if (!uuid.empty() && normalizedId(j["id"].get<std::string>()) != normalizedId(uuid)) {
        why = "the account that is signed in is not the player Falcon was started for";
        return false;
    }
    if (!name.empty() && j.contains("name") && j["name"].is_string() && j["name"].get<std::string>() != name) {
        why = "the name of the account does not match";
        return false;
    }
    return true;
}

OwnershipCheck verifyMinecraftOwnership(const std::string& accessToken, const std::string& uuid, const std::string& name) {
    OwnershipCheck result;
    if (accessToken.empty()) {
        result.message = "Falcon has to be started from the Falcon Launcher: it needs the Minecraft account you are signed in with.";
        return result;
    }
    const auto expired = [](int status) { return status == 401 || status == 403; };
    const HttpAnswer entitlements = httpsGet("api.minecraftservices.com", "/entitlements/mcstore", accessToken);
    if (entitlements.status == 0) {
        result.message = "Falcon could not reach Minecraft's servers to check that you own the game (" + entitlements.error + "). Check your internet connection and start it again.";
        return result;
    }
    if (expired(entitlements.status)) {
        result.message = "Your Minecraft sign-in has expired. Start Falcon from the launcher again.";
        return result;
    }
    if (entitlements.status != 200 || !entitlementsListGame(entitlements.body)) {
        result.message = "This Minecraft account does not own Minecraft: Java Edition, so Falcon cannot be played with it.";
        return result;
    }
    const HttpAnswer profile = httpsGet("api.minecraftservices.com", "/minecraft/profile", accessToken);
    if (profile.status == 0) {
        result.message = "Falcon could not reach Minecraft's servers to check your profile (" + profile.error + ").";
        return result;
    }
    if (profile.status != 200) {
        result.message = expired(profile.status) ? "Your Minecraft sign-in has expired. Start Falcon from the launcher again."
                                                 : "This Minecraft account has no Java Edition profile, so Falcon cannot be played with it.";
        return result;
    }
    std::string why;
    if (!profileMatches(profile.body, uuid, name, why)) {
        result.message = "Falcon cannot be played like this: " + why + ".";
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace falcon
