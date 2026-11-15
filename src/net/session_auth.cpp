#include "falcon/net/session_auth.hpp"

#include "falcon/platform/http.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace falcon::net {

namespace {

// Days from 1970-01-01 to a date of the (proleptic Gregorian) calendar.
std::int64_t daysFromCivil(std::int64_t y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::string serverWords(const HttpAnswer& answer) {
    // The session server answers with {"error": ..., "errorMessage": ...}.
    const nlohmann::json j = nlohmann::json::parse(answer.body, nullptr, false);
    if (j.is_object() && j.contains("errorMessage") && j["errorMessage"].is_string()) return j["errorMessage"].get<std::string>();
    return {};
}

}  // namespace

Bytes base64Decode(const std::string& text) {
    Bytes out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else if (std::isspace(static_cast<unsigned char>(c))) continue;
        else return {};
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

Bytes pemBody(const std::string& pem) {
    std::string body;
    std::size_t at = 0;
    while (at < pem.size()) {
        std::size_t end = pem.find('\n', at);
        if (end == std::string::npos) end = pem.size();
        const std::string line = pem.substr(at, end - at);
        at = end + 1;
        if (line.compare(0, 5, "-----") == 0) continue;  // the BEGIN and END lines
        body += line;
    }
    return base64Decode(body);
}

std::int64_t parseIsoMillis(const std::string& text) {
    int y, mo, d, h, mi, s;
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return -1;
    std::int64_t millis = ((daysFromCivil(y, mo, d) * 24 + h) * 60 + mi) * 60 + s;
    millis *= 1000;
    const std::size_t dot = text.find('.');
    if (dot != std::string::npos) {  // the fraction of a second: its first three digits
        int scale = 100;
        for (std::size_t i = dot + 1; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && scale > 0; ++i, scale /= 10) millis += (text[i] - '0') * scale;
    }
    return millis;
}

bool parseUuid(const std::string& text, std::array<std::uint8_t, 16>& out) {
    int digits = 0;
    for (const char c : text) {
        if (c == '-') continue;
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return false;
        if (digits >= 32) return false;
        if ((digits & 1) == 0) out[static_cast<std::size_t>(digits / 2)] = static_cast<std::uint8_t>(v << 4);
        else out[static_cast<std::size_t>(digits / 2)] = static_cast<std::uint8_t>(out[static_cast<std::size_t>(digits / 2)] | v);
        ++digits;
    }
    return digits == 32;
}

std::string uuidWithoutDashes(const std::array<std::uint8_t, 16>& id) {
    static const char* const kHex = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : id) {
        s += kHex[b >> 4];
        s += kHex[b & 15];
    }
    return s;
}

Bytes chatSignaturePayload(const std::array<std::uint8_t, 16>& sender, const std::array<std::uint8_t, 16>& session, int index, std::int64_t salt, std::int64_t seconds, const std::string& text,
                         const std::vector<std::array<std::uint8_t, 256>>& seen) {
    Writer w;
    w.i32(1);
    w.uuid(sender);
    w.uuid(session);
    w.i32(index);
    w.i64(salt);
    w.i64(seconds);
    w.i32(static_cast<std::int32_t>(text.size()));
    w.raw(Bytes(text.begin(), text.end()));
    w.i32(static_cast<std::int32_t>(seen.size()));
    for (const auto& signature : seen) w.raw(Bytes(signature.begin(), signature.end()));
    return std::move(w.data);
}

bool parseChatKeysResponse(const std::string& body, ChatKeys& out, std::string& error) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("keyPair") || !j["keyPair"].is_object()) {
        error = "Minecraft sent no chat keys";
        return false;
    }
    const nlohmann::json& pair = j["keyPair"];
    if (!pair.contains("privateKey") || !pair["privateKey"].is_string() || !pair.contains("publicKey") || !pair["publicKey"].is_string()) {
        error = "the chat keys are incomplete";
        return false;
    }
    out.privateKey = pemBody(pair["privateKey"].get<std::string>());
    out.publicKey = pemBody(pair["publicKey"].get<std::string>());
    // The signature that includes the player's id (V2) is what servers check since 1.19.1.
    const char* const names[] = {"publicKeySignatureV2", "publicKeySignature"};
    for (const char* name : names) {
        if (j.contains(name) && j[name].is_string()) {
            out.signature = base64Decode(j[name].get<std::string>());
            if (!out.signature.empty()) break;
        }
    }
    out.expiresAtMillis = j.contains("expiresAt") && j["expiresAt"].is_string() ? parseIsoMillis(j["expiresAt"].get<std::string>()) : -1;
    if (out.privateKey.empty() || out.publicKey.empty() || out.signature.empty() || out.expiresAtMillis < 0) {
        error = "the chat keys cannot be read";
        return false;
    }
    return true;
}

bool joinSession(const std::string& accessToken, const std::string& profileId, const std::string& serverHash, std::string& error) {
    std::array<std::uint8_t, 16> id{};
    if (!parseUuid(profileId, id)) {
        error = "the account's id is not known: start Falcon from the launcher";
        return false;
    }
    nlohmann::json body;
    body["accessToken"] = accessToken;
    body["selectedProfile"] = uuidWithoutDashes(id);
    body["serverId"] = serverHash;
    const HttpAnswer answer = httpsRequestPatient("POST", "sessionserver.mojang.com", "/session/minecraft/join", {}, body.dump());
    if (answer.status == 204 || answer.status == 200) return true;
    if (answer.status == 0) {
        error = "Falcon could not reach Minecraft's session server (" + answer.error + ")";
    } else if (answer.status == 401 || answer.status == 403) {
        const std::string words = serverWords(answer);
        error = "Minecraft's session server did not accept the sign-in" + (words.empty() ? std::string() : " (" + words + ")") + ": start Falcon from the launcher again";
    } else if (answer.status == 429 || answer.status >= 500) {
        error = busyAnswerWords(answer);
    } else {
        error = "Minecraft's session server answered " + std::to_string(answer.status);
    }
    return false;
}

bool fetchProfile(const std::string& accessToken, std::string& id, std::string& name, std::string& error) {
    const HttpAnswer answer = httpsRequestPatient("GET", "api.minecraftservices.com", "/minecraft/profile", accessToken);
    if (answer.status == 0) {
        error = "Falcon could not reach Minecraft's servers (" + answer.error + ")";
        return false;
    }
    if (answer.status == 429 || answer.status >= 500) {
        error = busyAnswerWords(answer);
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(answer.body, nullptr, false);
    if (answer.status != 200 || !j.is_object() || !j.contains("id") || !j["id"].is_string() || !j.contains("name") || !j["name"].is_string()) {
        error = answer.status == 401 || answer.status == 403 ? "Your Minecraft sign-in has expired: start Falcon from the launcher again" : "Minecraft sent no profile for this account";
        return false;
    }
    id = j["id"].get<std::string>();
    name = j["name"].get<std::string>();
    return true;
}

namespace {

// Where the keys are kept (the game keeps them too, in .minecraft/profilekeys): one file per account, the answer of the server as it came.
std::filesystem::path keyCachePath(const std::string& profileId) {
    std::array<std::uint8_t, 16> id{};
    if (!parseUuid(profileId, id)) return {};
    const char* local = std::getenv("LOCALAPPDATA");
    if (local == nullptr || *local == '\0') return {};
    return std::filesystem::path(local) / "Falcon" / "profilekeys" / (uuidWithoutDashes(id) + ".json");
}

std::int64_t nowMillis() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// The kept keys when there are some that are still good: not expired (with an hour to spare when they are to be used as they are) and, for a fresh use, not older than
// six hours (the server says when they ought to be renewed, about eight hours in).
bool readKeyCache(const std::filesystem::path& path, bool needFresh, ChatKeys& out) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return false;
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    ChatKeys keys;
    std::string ignored;
    if (!parseChatKeysResponse(buffer.str(), keys, ignored)) return false;
    const std::int64_t now = nowMillis();
    if (keys.expiresAtMillis <= now + (needFresh ? 3600000 : 0)) return false;
    if (needFresh) {
        const auto written = std::filesystem::last_write_time(path, ec);
        if (ec) return false;
        const auto age = std::filesystem::file_time_type::clock::now() - written;
        if (age > std::chrono::hours(6)) return false;
    }
    out = std::move(keys);
    return true;
}

}  // namespace

bool fetchChatKeys(const std::string& accessToken, const std::string& profileId, ChatKeys& out, std::string& error) {
    const std::filesystem::path cache = keyCachePath(profileId);
    if (readKeyCache(cache, /*needFresh=*/true, out)) return true;  // (the game asks Mojang only when its keys are about to run out: the request is limited hard)
    const HttpAnswer answer = httpsRequestPatient("POST", "api.minecraftservices.com", "/player/certificates", accessToken);
    if (answer.status == 200 && parseChatKeysResponse(answer.body, out, error)) {
        std::error_code ec;
        if (!cache.empty()) {
            std::filesystem::create_directories(cache.parent_path(), ec);
            std::ofstream file(cache, std::ios::binary | std::ios::trunc);
            file << answer.body;
        }
        return true;
    }
    if (answer.status == 200) return false;  // (the answer could not be read: error is set)
    if (readKeyCache(cache, /*needFresh=*/false, out)) return true;  // refused or not reachable: the keys that were kept still work until they expire
    if (answer.status == 0) {
        error = "Falcon could not reach Minecraft's servers for the chat keys (" + answer.error + ")";
    } else if (answer.status == 429 || answer.status >= 500) {
        error = busyAnswerWords(answer);
    } else {
        error = "Minecraft would not give the chat keys (it answered " + std::to_string(answer.status) + ")";
    }
    return false;
}

}  // namespace falcon::net
