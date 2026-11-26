// Tests of the launcher's logic: no window, no network (a fake HTTP client answers), nothing outside a temporary folder.
#include "app.hpp"
#include "avatar.hpp"
#include "auth.hpp"
#include "game_files.hpp"
#include "http.hpp"
#include "mods.hpp"
#include "secure_store.hpp"
#include "setup.hpp"
#include "sha1.hpp"
#include "util.hpp"

#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <sys/stat.h>
#define _putenv_s(name, value) setenv((name), (value), 1)  // (the Windows name for it, which the tests use)
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

using namespace launcher;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);        \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

// ---- a network that answers from a table ----------------------------------------------------------------------------------

class FakeHttp final : public HttpClient {
public:
    struct Call {
        std::string method, url, body;
        HttpHeaders headers;
    };
    // The replies for "METHOD url", given out in order; the last one repeats.
    void on(const std::string& method, const std::string& url, std::vector<HttpResponse> replies) {
        std::lock_guard<std::mutex> lock(mutex_);
        routes_[method + " " + url] = std::move(replies);
    }
    static HttpResponse reply(int status, std::string body) {
        HttpResponse r;
        r.status = status;
        r.body = std::move(body);
        return r;
    }
    static HttpResponse unreachable() {
        HttpResponse r;
        r.error = "no internet";
        return r;
    }
    [[nodiscard]] std::vector<Call> calls() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_;
    }
    [[nodiscard]] int count(const std::string& method, const std::string& url) const {
        int n = 0;
        for (const Call& c : calls()) n += c.method == method && c.url == url ? 1 : 0;
        return n;
    }
    [[nodiscard]] Call last(const std::string& method, const std::string& url) const {
        Call found;
        for (const Call& c : calls()) {
            if (c.method == method && c.url == url) found = c;
        }
        return found;
    }

    HttpResponse request(const std::string& method, const std::string& url, const HttpHeaders& headers, const std::string& body) override {
        std::lock_guard<std::mutex> lock(mutex_);
        calls_.push_back({method, url, body, headers});
        return next(method, url);
    }
    HttpResponse download(const std::string& url, const fs::path& to, const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                          const std::atomic<bool>* cancel) override {
        std::lock_guard<std::mutex> lock(mutex_);
        calls_.push_back({"DOWNLOAD", url, {}, {}});
        if (cancel != nullptr && cancel->load()) {
            HttpResponse r;
            r.error = "cancelled";
            return r;
        }
        HttpResponse r = next("GET", url);
        if (r.ok()) {
            std::error_code ec;
            fs::create_directories(to.parent_path(), ec);
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            out.write(r.body.data(), static_cast<std::streamsize>(r.body.size()));
            if (progress) progress(r.body.size(), r.body.size());
        }
        return r;
    }

private:
    HttpResponse next(const std::string& method, const std::string& url) {
        const auto it = routes_.find(method + " " + url);
        if (it == routes_.end() || it->second.empty()) return reply(404, "{}");
        HttpResponse r = it->second.front();
        if (it->second.size() > 1) it->second.erase(it->second.begin());
        return r;
    }
    mutable std::mutex mutex_;
    std::map<std::string, std::vector<HttpResponse>> routes_;
    std::vector<Call> calls_;
};

fs::path freshFolder(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("falcon_launcher_test_") + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// ---- a zip, as Mojang's client jar is one ------------------------------------------------------------------------------------

std::uint32_t crc32(const std::string& data) {
    std::uint32_t table[256];
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

std::string makeZip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, central;
    const auto u16 = [](std::string& s, unsigned v) { s.push_back(static_cast<char>(v & 0xFF)); s.push_back(static_cast<char>((v >> 8) & 0xFF)); };
    const auto u32 = [&](std::string& s, std::uint32_t v) { u16(s, v & 0xFFFF); u16(s, v >> 16); };
    for (const auto& [name, data] : files) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t crc = crc32(data);
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0); u16(out, 0); u16(out, 0);
        u32(out, crc); u32(out, static_cast<std::uint32_t>(data.size())); u32(out, static_cast<std::uint32_t>(data.size()));
        u16(out, static_cast<unsigned>(name.size())); u16(out, 0);
        out += name + data;
        u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0);
        u32(central, crc); u32(central, static_cast<std::uint32_t>(data.size())); u32(central, static_cast<std::uint32_t>(data.size()));
        u16(central, static_cast<unsigned>(name.size())); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0); u32(central, 0); u32(central, offset);
        central += name;
    }
    const std::uint32_t centralOffset = static_cast<std::uint32_t>(out.size());
    out += central;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, static_cast<unsigned>(files.size())); u16(out, static_cast<unsigned>(files.size()));
    u32(out, static_cast<std::uint32_t>(central.size())); u32(out, centralOffset); u16(out, 0);
    return out;
}

// ---- the services' answers -----------------------------------------------------------------------------------------------------

const char* kTokenUrl = "https://login.live.com/oauth20_token.srf";
const char* kXblUrl = "https://user.auth.xboxlive.com/user/authenticate";
const char* kXstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
const char* kMcLoginUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
const char* kEntitlementsUrl = "https://api.minecraftservices.com/entitlements/mcstore";
const char* kProfileUrl = "https://api.minecraftservices.com/minecraft/profile";

void serveXboxChain(FakeHttp& http, bool owns, bool profile) {
    http.on("POST", kXblUrl, {FakeHttp::reply(200, R"({"Token":"xbl-token","DisplayClaims":{"xui":[{"uhs":"user-hash"}]}})")});
    http.on("POST", kXstsUrl, {FakeHttp::reply(200, R"({"Token":"xsts-token","DisplayClaims":{"xui":[{"uhs":"user-hash"}]}})")});
    http.on("POST", kMcLoginUrl, {FakeHttp::reply(200, R"({"access_token":"mc-token","expires_in":86400})")});
    http.on("GET", kEntitlementsUrl, {FakeHttp::reply(200, owns ? R"({"items":[{"name":"product_minecraft"},{"name":"game_minecraft"}]})" : R"({"items":[]})")});
    http.on("GET", kProfileUrl, {profile ? FakeHttp::reply(200, R"({"id":"069a79f444e94726a5befca90e38aaf5","name":"Steve","skins":[{"id":"1","state":"INACTIVE","url":"http://textures.minecraft.net/texture/old"},{"id":"2","state":"ACTIVE","url":"http://textures.minecraft.net/texture/abc123"}]})") : FakeHttp::reply(404, R"({"error":"NOT_FOUND"})")});
}

// A plain 64x64 skin (an opaque green face and nothing else), as PNG bytes.
std::string flatSkinPng() {
    std::vector<unsigned char> skin(64 * 64 * 4, 0);
    for (int y = 8; y < 16; ++y) {
        for (int x = 8; x < 16; ++x) {
            unsigned char* px = &skin[(static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)) * 4];
            px[0] = 60, px[1] = 180, px[2] = 90, px[3] = 255;
        }
    }
    std::string png;
    stbi_write_png_to_func([](void* context, void* data, int size) { static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size)); }, &png, 64, 64, 4,
                           skin.data(), 64 * 4);
    return png;
}


// ---- tests -----------------------------------------------------------------------------------------------------------------------

void test_hash_and_encoding() {
    CHECK(sha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(sha1Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    const fs::path dir = freshFolder("hash");
    writeFileAtomic(dir / "f.bin", "abc");
    CHECK(sha1FileHex(dir / "f.bin") == "a9993e364706816aba3e25717850c26c9cd0d89d" && sha1FileHex(dir / "missing").empty());
    CHECK(formEncode({{"a b", "c&d=e"}, {"x", "~ok-_."}}) == "a%20b=c%26d%3De&x=~ok-_.");
    CHECK(urlEncode("é") == "%C3%A9");
    CHECK(narrow(widen("Falcon é ✓")) == "Falcon é ✓");
    CHECK(randomHex(8).size() == 16 && randomHex(8) != randomHex(8));
}

void test_safe_paths_and_urls() {
    CHECK(GameFiles::isSafeRelativePath("assets/minecraft/lang/en_us.json") && GameFiles::isSafeRelativePath("a"));
    for (const char* bad : {"", "/etc", "../x", "a/../b", "a//b", "a/./b", "C:/x", "a\\b", "a/", ".."}) CHECK(!GameFiles::isSafeRelativePath(bad));
    CHECK(isSafeWebUrl("https://www.microsoft.com/link") && !isSafeWebUrl("http://x.com") && !isSafeWebUrl("file:///c:/x") && !isSafeWebUrl("https://x.com/a b") &&
          !isSafeWebUrl("javascript:alert(1)") && !isSafeWebUrl("https://") && !isSafeWebUrl("https://x.com/\"onclick"));
}

void test_secure_store() {
    const fs::path file = freshFolder("store") / "account.bin";
    CHECK(protectToFile(file, "refresh-token-123"));
    std::string raw;
#ifdef _WIN32
    CHECK(readFile(file, raw) && raw.find("refresh-token-123") == std::string::npos);  // not readable on disk
    CHECK(unprotectFromFile(file) == std::optional<std::string>("refresh-token-123"));
    raw[raw.size() / 2] = static_cast<char>(raw[raw.size() / 2] ^ 0x55);  // damaged: refused, not trusted
    writeFileAtomic(file, raw);
    CHECK(!unprotectFromFile(file).has_value() && !unprotectFromFile(file.parent_path() / "missing").has_value());
#else
    // Linux keeps it in a file only this user can read (there is no per-user encryption to call): the contents are as they were given, the mode is 0600, nothing else is left behind.
    CHECK(readFile(file, raw) && raw == "refresh-token-123");
    CHECK(unprotectFromFile(file) == std::optional<std::string>("refresh-token-123"));
    struct stat info{};
    CHECK(stat(file.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    CHECK(!fs::exists(fs::path(file.string() + ".tmp")));
    CHECK(protectToFile(file, "replaced") && unprotectFromFile(file) == std::optional<std::string>("replaced"));
    CHECK(!unprotectFromFile(file.parent_path() / "missing").has_value());
#endif
}

void test_browser_sign_in() {
    FakeHttp http;
    AuthService off(http, "");
    CHECK(!off.configured() && off.exchangeCode("c").error.code == "not_configured");

    AuthService auth(http, kDefaultMinecraftClientId);
    CHECK(auth.configured());
    // the address of Microsoft's page: the launcher's public client id, the redirect the official launcher uses, the Xbox ticket scope, our state
    const std::string url = auth.authorizeUrl("st4te");
    CHECK(url.rfind("https://login.live.com/oauth20_authorize.srf?", 0) == 0);
    CHECK(url.find("client_id=00000000402b5328") != std::string::npos && url.find("response_type=code") != std::string::npos);
    CHECK(url.find("redirect_uri=https%3A%2F%2Flogin.live.com%2Foauth20_desktop.srf") != std::string::npos);
    CHECK(url.find("scope=service%3A%3Auser.auth.xboxlive.com%3A%3AMBI_SSL") != std::string::npos && url.find("state=st4te") != std::string::npos);

    // what the page ends at
    AuthRedirect r = AuthService::parseRedirect("https://login.live.com/oauth20_desktop.srf?code=M.C5_BAY.2.U.abc-123&lc=1033&state=st4te");
    CHECK(r.code == "M.C5_BAY.2.U.abc-123" && r.state == "st4te" && r.error.empty());
    r = AuthService::parseRedirect("https://login.live.com/oauth20_desktop.srf?error=access_denied&error_description=The+user+has+denied%20access.&state=s");
    CHECK(r.error == "access_denied" && r.errorDescription == "The user has denied access." && r.code.empty());
    r = AuthService::parseRedirect("https://login.live.com/oauth20_desktop.srf#code=frag&state=s");  // (some flows answer in the fragment)
    CHECK(r.code == "frag" && r.state == "s");
    CHECK(AuthService::parseRedirect("https://login.live.com/oauth20_desktop.srf").code.empty());
    CHECK(AuthService::parseRedirect("https://login.live.com/oauth20_desktop.srfx?code=a").code.empty());  // not the redirect
    CHECK(AuthService::parseRedirect("https://evil.example/oauth20_desktop.srf?code=a").code.empty() && AuthService::parseRedirect("").code.empty());
    CHECK(urlDecode("a%20b+c%2") == "a b c%2" && urlDecode("%e2%9c%93") == "\xE2\x9C\x93");

    // the code is traded for tokens
    http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"access_token":"ms-access","refresh_token":"ms-refresh","expires_in":3600})")});
    const auto tokens = auth.exchangeCode("thecode");
    CHECK(tokens.ok() && tokens->accessToken == "ms-access" && tokens->refreshToken == "ms-refresh" && tokens->expiresInSeconds == 3600);
    const std::string sent = http.last("POST", kTokenUrl).body;
    CHECK(sent.find("grant_type=authorization_code") != std::string::npos && sent.find("code=thecode") != std::string::npos);
    CHECK(sent.find("client_id=00000000402b5328") != std::string::npos && sent.find("redirect_uri=https%3A%2F%2Flogin.live.com%2Foauth20_desktop.srf") != std::string::npos);

    // Microsoft refusing the code, garbage, and no network
    http.on("POST", kTokenUrl, {FakeHttp::reply(400, R"({"error":"invalid_grant","error_description":"The code has expired."})")});
    const auto refused = auth.exchangeCode("old");
    CHECK(refused.error.code == "service" && refused.error.message.find("The code has expired.") != std::string::npos);
    http.on("POST", kTokenUrl, {FakeHttp::reply(200, "not json")});
    CHECK(auth.exchangeCode("x").error.code == "bad_response");
    http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"refresh_token":"only"})")});
    CHECK(auth.exchangeCode("x").error.code == "bad_response");
    http.on("POST", kTokenUrl, {FakeHttp::unreachable()});
    CHECK(auth.exchangeCode("x").error.code == "network");
}

void test_refresh() {
    FakeHttp http;
    AuthService auth(http, kDefaultMinecraftClientId);
    http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"access_token":"new-access","expires_in":3600})")});
    const auto kept = auth.refresh("old-refresh");  // Microsoft did not rotate the refresh token: the old one stays
    CHECK(kept.ok() && kept->accessToken == "new-access" && kept->refreshToken == "old-refresh");
    const std::string sent = http.last("POST", kTokenUrl).body;
    CHECK(sent.find("grant_type=refresh_token") != std::string::npos && sent.find("refresh_token=old-refresh") != std::string::npos && sent.find("client_id=00000000402b5328") != std::string::npos);
    http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"access_token":"a2","refresh_token":"rotated"})")});
    CHECK(auth.refresh("old-refresh")->refreshToken == "rotated");
    http.on("POST", kTokenUrl, {FakeHttp::reply(400, R"({"error":"invalid_grant"})")});
    CHECK(auth.refresh("revoked").error.code == "invalid_grant");
    http.on("POST", kTokenUrl, {FakeHttp::unreachable()});
    CHECK(auth.refresh("x").error.code == "network");
}

// The skin's face and hat, cut out and enlarged.
void test_avatar() {
    // a 64x64 skin: face (8,8 .. 15,15) green, a hat layer (40,8 ..) transparent except its first pixel (opaque red) and one half-transparent blue
    std::vector<unsigned char> skin(64 * 64 * 4, 0);
    const auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        unsigned char* px = &skin[(static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)) * 4];
        px[0] = r, px[1] = g, px[2] = b, px[3] = a;
    };
    for (int y = 8; y < 16; ++y) {
        for (int x = 8; x < 16; ++x) put(x, y, 10, 200, 30, 255);
    }
    put(40, 8, 255, 0, 0, 255);
    put(41, 8, 0, 0, 255, 128);
    std::string png;
    stbi_write_png_to_func([](void* context, void* data, int size) { static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size)); }, &png, 64, 64, 4,
                           skin.data(), 64 * 4);
    CHECK(!png.empty());

    const std::string uri = avatarFromSkin(png, 4);
    const std::string prefix = "data:image/png;base64,";
    CHECK(uri.rfind(prefix, 0) == 0);
    // decode our own answer and look at the pixels
    const auto decode64 = [](const std::string& text) {
        std::string out;
        unsigned buffer = 0;
        int bits = 0;
        for (const char c : text) {
            const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const char* at = c == '=' ? nullptr : std::strchr(digits, c);
            if (at == nullptr || c == '\0') continue;
            buffer = (buffer << 6) | static_cast<unsigned>(at - digits);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
            }
        }
        return out;
    };
    const std::string decoded = decode64(uri.substr(prefix.size()));
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(decoded.data()), static_cast<int>(decoded.size()), &w, &h, &n, 4);
    CHECK(px != nullptr && w == 32 && h == 32);
    if (px != nullptr && w == 32 && h == 32) {
        const auto at = [&](int x, int y) { return px + (y * 32 + x) * 4; };
        CHECK(at(0, 0)[0] == 255 && at(0, 0)[1] == 0 && at(3, 3)[0] == 255);    // the hat's opaque pixel covers its whole 4x4 block
        CHECK(at(31, 31)[1] == 200 && at(16, 16)[0] == 10 && at(16, 16)[3] == 255);  // the face elsewhere
        const unsigned char* mixed = at(4, 0);                                    // a half-transparent hat pixel mixes with the face
        CHECK(mixed[2] > 100 && mixed[2] < 160 && mixed[1] > 80 && mixed[1] < 120);
    }
    if (px != nullptr) stbi_image_free(px);

    CHECK(avatarFromSkin("not a png").empty() && avatarFromSkin("").empty());
    // a picture that is not a skin (wrong shape) gives nothing
    std::vector<unsigned char> square(20 * 20 * 4, 255);
    std::string other;
    stbi_write_png_to_func([](void* context, void* data, int size) { static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size)); }, &other, 20, 20, 4,
                           square.data(), 20 * 4);
    CHECK(avatarFromSkin(other).empty());

    CHECK(isSkinUrl("http://textures.minecraft.net/texture/abc123") && isSkinUrl("https://textures.minecraft.net/texture/abc123"));
    CHECK(!isSkinUrl("https://evil.example/texture/abc") && !isSkinUrl("https://textures.minecraft.net.evil.example/x") && !isSkinUrl("https://textures.minecraft.net/") && !isSkinUrl(""));
    CHECK(!isSkinUrl("https://textures.minecraft.net/a b") && !isSkinUrl("file:///c:/skin.png"));
    CHECK(base64("") == "" && base64("f") == "Zg==" && base64("fo") == "Zm8=" && base64("foo") == "Zm9v" && base64("foob") == "Zm9vYg==");
}

void test_minecraft_account() {
    {  // an owner with a profile, and exactly what is sent along the chain
        FakeHttp http;
        serveXboxChain(http, true, true);
        AuthService auth(http, "client-id");
        const auto account = auth.minecraftAccount("ms-access");
        CHECK(account.ok() && account->ownsGame && account->hasProfile && account->name == "Steve" && account->uuid == "069a79f444e94726a5befca90e38aaf5" && account->accessToken == "mc-token");
        const json xbl = json::parse(http.last("POST", kXblUrl).body);
        CHECK(xbl["Properties"]["RpsTicket"] == "d=ms-access" && xbl["RelyingParty"] == "http://auth.xboxlive.com");
        const json xsts = json::parse(http.last("POST", kXstsUrl).body);
        CHECK(xsts["Properties"]["UserTokens"][0] == "xbl-token" && xsts["RelyingParty"] == "rp://api.minecraftservices.com/");
        CHECK(json::parse(http.last("POST", kMcLoginUrl).body)["identityToken"] == "XBL3.0 x=user-hash;xsts-token");
        bool bearer = false;
        for (const auto& [k, v] : http.last("GET", kEntitlementsUrl).headers) bearer = bearer || (k == "Authorization" && v == "Bearer mc-token");
        CHECK(bearer);
    }
    {  // the active skin is the one reported
        FakeHttp http;
        serveXboxChain(http, true, true);
        CHECK(AuthService(http, "c").minecraftAccount("t")->skinUrl == "http://textures.minecraft.net/texture/abc123");
    }
    {  // the first ticket format is refused at once, the second is accepted
        FakeHttp http;
        serveXboxChain(http, true, true);
        http.on("POST", kXblUrl, {FakeHttp::reply(400, R"({"XErr":2148916238})"), FakeHttp::reply(200, R"({"Token":"xbl-token","DisplayClaims":{"xui":[{"uhs":"user-hash"}]}})")});
        CHECK(AuthService(http, "c").minecraftAccount("tok").ok());
        CHECK(http.count("POST", kXblUrl) == 2 && json::parse(http.last("POST", kXblUrl).body)["Properties"]["RpsTicket"] == "t=tok");
    }
    {  // an account that owns nothing: not a failure, the UI explains it
        FakeHttp http;
        serveXboxChain(http, false, false);
        const auto account = AuthService(http, "c").minecraftAccount("t");
        CHECK(account.ok() && !account->ownsGame && !account->hasProfile);
    }
    {  // owns the game (Game Pass shows only game_minecraft) but never chose a name
        FakeHttp http;
        serveXboxChain(http, true, false);
        http.on("GET", kEntitlementsUrl, {FakeHttp::reply(200, R"({"items":[{"name":"game_minecraft"}]})")});
        const auto account = AuthService(http, "c").minecraftAccount("t");
        CHECK(account.ok() && account->ownsGame && !account->hasProfile);
    }
    // what Xbox says when the account cannot be used
    const std::pair<long long, const char*> xerrors[] = {{2148916233LL, "xbox_no_account"}, {2148916235LL, "xbox_region"}, {2148916236LL, "xbox_adult"}, {2148916237LL, "xbox_adult"},
                                                         {2148916238LL, "xbox_child"}, {1234LL, "xbox_denied"}};
    for (const auto& [xerr, code] : xerrors) {
        FakeHttp http;
        serveXboxChain(http, true, true);
        http.on("POST", kXstsUrl, {FakeHttp::reply(401, "{\"Identity\":\"0\",\"XErr\":" + std::to_string(xerr) + "}")});
        CHECK(AuthService(http, "c").minecraftAccount("t").error.code == code);
    }
    {  // Mojang has not approved the application
        FakeHttp http;
        serveXboxChain(http, true, true);
        http.on("POST", kMcLoginUrl, {FakeHttp::reply(403, R"({"path":"/authentication/login_with_xbox","errorMessage":"Invalid app registration, see https://aka.ms/AppRegInfo for more information"})")});
        CHECK(AuthService(http, "c").minecraftAccount("t").error.code == "app_not_approved");
    }
    {  // services down / garbage
        FakeHttp http;
        serveXboxChain(http, true, true);
        http.on("POST", kXblUrl, {FakeHttp::unreachable()});
        CHECK(AuthService(http, "c").minecraftAccount("t").error.code == "network");
        http.on("POST", kXblUrl, {FakeHttp::reply(200, R"({"Token":"x"})")});  // no user hash
        CHECK(AuthService(http, "c").minecraftAccount("t").error.code == "bad_response");
        serveXboxChain(http, true, true);
        http.on("GET", kProfileUrl, {FakeHttp::reply(500, "{}")});
        CHECK(!AuthService(http, "c").minecraftAccount("t").ok());
    }
}

// The pieces of Mojang's answers, and a whole install against a fake Mojang.
struct FakeMojang {
    std::string tablesZip;
    DataTablesSource tables;
    std::string jar;
    std::string jarSha, indexText, indexSha;
    std::map<std::string, std::string> objects;  // hash -> bytes
    std::string versionJson, manifest;
};

FakeMojang buildMojang(FakeHttp& http, const std::string& version) {
    FakeMojang m;
    m.jar = makeZip({{"assets/minecraft/textures/block/stone.png", "PNGDATA"},
                     {"assets/minecraft/lang/en_us.json", "{\"a\":1}"},
                     {"data/minecraft/tags/block/dirt.json", "{\"values\":[]}"},
                     {"META-INF/MANIFEST.MF", "Manifest"},                    // not wanted
                     {"net/minecraft/Main.class", "CAFEBABE"},               // never unpacked: no game code
                     {"assets/../evil.txt", "x"}});                          // an unsafe name is skipped
    m.jarSha = sha1Hex(m.jar);
    const std::string sound = "OGGSOUND", lang = "{\"hello\":\"hallo\"}";
    const std::string soundHash = sha1Hex(sound), langHash = sha1Hex(lang), iconHash = sha1Hex("ICON");
    m.objects[soundHash] = sound;
    m.objects[langHash] = lang;
    m.objects[iconHash] = "ICON";
    json index;
    index["objects"]["minecraft/sounds/mob/cow/say1.ogg"] = {{"hash", soundHash}, {"size", sound.size()}};
    index["objects"]["minecraft/lang/de_de.json"] = {{"hash", langHash}, {"size", lang.size()}};
    index["objects"]["icons/icon_16x16.png"] = {{"hash", iconHash}, {"size", 4}};
    m.indexText = index.dump();
    m.indexSha = sha1Hex(m.indexText);
    json versionDoc;
    versionDoc["id"] = version;
    versionDoc["downloads"]["client"] = {{"url", "https://piston-data.mojang.com/v1/objects/client.jar"}, {"sha1", m.jarSha}, {"size", m.jar.size()}};
    versionDoc["assetIndex"] = {{"id", "30"}, {"url", "https://piston-meta.mojang.com/v1/packages/index.json"}, {"sha1", m.indexSha}, {"size", m.indexText.size()}};
    m.versionJson = versionDoc.dump();
    json manifestDoc;
    manifestDoc["latest"]["release"] = version;
    manifestDoc["versions"] = json::array();
    manifestDoc["versions"].push_back({{"id", "1.0"}, {"url", "https://piston-meta.mojang.com/v1/packages/old.json"}});
    manifestDoc["versions"].push_back({{"id", version}, {"url", "https://piston-meta.mojang.com/v1/packages/version.json"}});
    m.manifest = manifestDoc.dump();
    {  // Falcon's data tables: a zip of every table (and a file that is not one, which must not be unpacked)
        std::vector<std::pair<std::string, std::string>> tables;
        for (const std::string& name : GameFiles::dataTableFiles()) tables.emplace_back(name, "{\"table\":\"" + name + "\"}");
        tables.emplace_back("evil.txt", "x");
        m.tablesZip = makeZip(tables);
        m.tables = {"https://tables.test/falcon-data-26.3.zip", sha256Hex(m.tablesZip), m.tablesZip.size()};
        http.on("GET", m.tables.url, {FakeHttp::reply(200, m.tablesZip)});
    }
    http.on("GET", "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json", {FakeHttp::reply(200, m.manifest)});
    http.on("GET", "https://piston-meta.mojang.com/v1/packages/version.json", {FakeHttp::reply(200, m.versionJson)});
    http.on("GET", "https://piston-data.mojang.com/v1/objects/client.jar", {FakeHttp::reply(200, m.jar)});
    http.on("GET", "https://piston-meta.mojang.com/v1/packages/index.json", {FakeHttp::reply(200, m.indexText)});
    for (const auto& [hash, bytes] : m.objects) http.on("GET", GameFiles::assetObjectUrl(hash), {FakeHttp::reply(200, bytes)});
    return m;
}

void test_game_file_parsing() {
    FakeHttp http;
    const FakeMojang m = buildMojang(http, "26.3");
    CHECK(GameFiles::findVersionUrl(m.manifest, "26.3").ok() && *GameFiles::findVersionUrl(m.manifest, "26.3") == "https://piston-meta.mojang.com/v1/packages/version.json");
    CHECK(GameFiles::findVersionUrl(m.manifest, "9.9").error.code == "no_version" && !GameFiles::findVersionUrl("nonsense", "26.3").ok());
    const auto info = GameFiles::parseVersionJson(m.versionJson);
    CHECK(info.ok() && info->clientSha1 == m.jarSha && info->assetIndexId == "30" && info->clientSize == m.jar.size());
    CHECK(!GameFiles::parseVersionJson(R"({"id":"x"})").ok() && !GameFiles::parseVersionJson(R"({"downloads":{"client":{"url":"http://x","sha1":"1"}}})").ok());
    const auto objects = GameFiles::parseAssetIndex(m.indexText);
    CHECK(objects.ok() && objects->size() == 2 && (*objects)[0].key == "minecraft/lang/de_de.json" && (*objects)[1].key == "minecraft/sounds/mob/cow/say1.ogg");  // sorted; icons/ is not wanted
    const auto unsafe = GameFiles::parseAssetIndex(R"({"objects":{"minecraft/../x":{"hash":"0123456789012345678901234567890123456789","size":1},"minecraft/ok":{"hash":"short","size":1}}})");
    CHECK(unsafe.ok() && unsafe->empty());
    CHECK(GameFiles::assetObjectUrl("ab0123") == "https://resources.download.minecraft.net/ab/ab0123");
}

void test_game_file_install() {
    FakeHttp http;
    const FakeMojang m = buildMojang(http, "26.3");
    const fs::path root = freshFolder("install");
    GameFiles files(http, root, m.tables);
    std::atomic<bool> cancel{false};
    std::vector<std::string> phases;
    std::mutex phasesMutex;
    const auto progress = [&](const InstallProgress& p) {
        std::lock_guard<std::mutex> lock(phasesMutex);
        if (phases.empty() || phases.back() != p.phase) phases.push_back(p.phase);
    };
    CHECK(!files.isInstalled("26.3"));
    const auto result = files.install("26.3", progress, cancel);
    CHECK(result.ok());
    if (!result.ok()) std::fprintf(stderr, "  install said: %s\n", result.error.message.c_str());
    const fs::path dir = files.versionDir("26.3");
    std::string text;
    CHECK(readFile(dir / "assets/minecraft/textures/block/stone.png", text) && text == "PNGDATA");
    CHECK(readFile(dir / "assets/minecraft/lang/en_us.json", text) && text == "{\"a\":1}");
    CHECK(readFile(dir / "data/minecraft/tags/block/dirt.json", text) && text == "{\"values\":[]}");
    CHECK(readFile(dir / "assets/minecraft/sounds/mob/cow/say1.ogg", text) && text == "OGGSOUND");
    CHECK(readFile(dir / "assets/minecraft/lang/de_de.json", text) && text == "{\"hello\":\"hallo\"}");
    CHECK(!fs::exists(dir / "META-INF") && !fs::exists(dir / "net") && !fs::exists(dir / "evil.txt") && !fs::exists(dir / "assets/icons"));  // only assets and data, nothing else
    CHECK(!fs::exists(dir / "client.jar") && fs::exists(dir / "asset-index.json") && fs::exists(dir / "installed.json"));
    CHECK(!phases.empty() && phases.front() == "version" && phases.back() == "done");
    // (progress reports are throttled, so a fast fake network may skip the in-between ones; the first, the list of sounds and the last are always sent)
    CHECK(std::find(phases.begin(), phases.end(), "assets") != phases.end());
    // Falcon's data tables came down with it: each table, nothing that is not one, the marker, and no zip left behind
    CHECK(readFile(dir / "registries.json", text) && text == "{\"table\":\"registries.json\"}" && readFile(dir / "cached-collision.nbt", text) && fs::exists(dir / "commands.json"));
    CHECK(!fs::exists(dir / "evil.txt") && !fs::exists(dir / "data-tables.zip"));
    CHECK(fs::exists(dir / "tables.json"));
    CHECK(files.hasTables("26.3"));
    CHECK(GameFiles::isGameDataDir(dir));
    CHECK(std::find(phases.begin(), phases.end(), "tables") != phases.end() && files.findGameData("26.3") == std::optional<fs::path>(dir));
    // isInstalled wants the sounds.json the client reads first; this fake jar has none: the marker alone is not enough
    CHECK(!files.isInstalled("26.3"));
    writeFileAtomic(dir / "assets/minecraft/sounds.json", "{}");
    CHECK(files.isInstalled("26.3"));

    // a second install fetches no jar and no sound again
    const int jarsBefore = http.count("DOWNLOAD", "https://piston-data.mojang.com/v1/objects/client.jar");
    CHECK(files.install("26.3", progress, cancel).ok());
    CHECK(http.count("DOWNLOAD", "https://piston-data.mojang.com/v1/objects/client.jar") == jarsBefore);
    CHECK(http.count("DOWNLOAD", GameFiles::assetObjectUrl(sha1Hex("OGGSOUND"))) == 1);
    CHECK(http.count("DOWNLOAD", m.tables.url) == 1);  // (and the tables are not fetched again)

    // tables that are not the ones this launcher names are fetched again (a launcher of a newer release names newer ones)
    {
        DataTablesSource newer = m.tables;
        newer.sha256 = std::string(64, 'a');
        GameFiles other(http, root, newer);
        CHECK(!other.hasTables("26.3") && !other.isInstalled("26.3"));
    }
    // a table zip that does not match its checksum is refused, and nothing of it is unpacked
    {
        FakeHttp tampered;
        const FakeMojang t = buildMojang(tampered, "26.3");
        tampered.on("GET", t.tables.url, {FakeHttp::reply(200, t.tablesZip + "tampered")});
        GameFiles refused(tampered, freshFolder("tampered"), t.tables);
        const auto r = refused.install("26.3", nullptr, cancel);
        CHECK(!r.ok() && r.error.code == "corrupt" && !fs::exists(refused.versionDir("26.3") / "registries.json") && !fs::exists(refused.versionDir("26.3") / "installed.json"));
        // a zip that is the right one but lacks a table is refused too
        std::vector<std::pair<std::string, std::string>> partial;
        for (const std::string& name : GameFiles::dataTableFiles()) {
            if (name != "items.json") partial.emplace_back(name, "{}");
        }
        const std::string zip = makeZip(partial);
        FakeHttp shortHttp;
        const FakeMojang u = buildMojang(shortHttp, "26.3");
        const DataTablesSource shortTables{u.tables.url, sha256Hex(zip), zip.size()};
        shortHttp.on("GET", u.tables.url, {FakeHttp::reply(200, zip)});
        GameFiles lacking(shortHttp, freshFolder("lacking"), shortTables);
        CHECK(lacking.install("26.3", nullptr, cancel).error.code == "corrupt" && !lacking.hasTables("26.3"));
        // no network for them: the install says so and can be run again
        FakeHttp noTables;
        const FakeMojang n = buildMojang(noTables, "26.3");
        noTables.on("GET", n.tables.url, {FakeHttp::unreachable()});
        GameFiles offlineTables(noTables, freshFolder("notables"), n.tables);
        CHECK(offlineTables.install("26.3", nullptr, cancel).error.code == "network" && !offlineTables.isInstalled("26.3"));
    }

    // a damaged download is refused
    FakeHttp bad;
    const FakeMojang b = buildMojang(bad, "26.3");
    bad.on("GET", "https://piston-data.mojang.com/v1/objects/client.jar", {FakeHttp::reply(200, b.jar + "tampered")});
    GameFiles damaged(bad, freshFolder("damaged"), b.tables);
    CHECK(damaged.install("26.3", nullptr, cancel).error.code == "corrupt");
    // a sound that does not match its hash fails the install after retries instead of leaving a bad file
    FakeHttp badSound;
    const FakeMojang s = buildMojang(badSound, "26.3");
    badSound.on("GET", GameFiles::assetObjectUrl(sha1Hex("OGGSOUND")), {FakeHttp::reply(200, "WRONG")});
    GameFiles unlucky(badSound, freshFolder("badsound"), s.tables);
    const auto failed = unlucky.install("26.3", nullptr, cancel);
    CHECK(!failed.ok() && !fs::exists(unlucky.versionDir("26.3") / "assets/minecraft/sounds/mob/cow/say1.ogg") && !fs::exists(unlucky.versionDir("26.3") / "installed.json"));
    // no network
    FakeHttp offline;
    offline.on("GET", "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json", {FakeHttp::unreachable()});
    GameFiles nowhere(offline, freshFolder("offline"));
    CHECK(nowhere.install("26.3", nullptr, cancel).error.code == "network");
    // cancelled before it starts
    std::atomic<bool> stop{true};
    FakeHttp quick;
    const FakeMojang q = buildMojang(quick, "26.3");
    GameFiles stopped(quick, freshFolder("cancelled"), q.tables);
    CHECK(stopped.install("26.3", nullptr, stop).error.code == "cancelled");
}

// Where the game's data tables are looked for: only in the launcher's own game folder (nothing of another launcher's is used).
void test_game_data_dir() {
    const fs::path root = freshFolder("gamedata_root");
    FakeHttp http;
    GameFiles files(http, root / "game");
    const auto makeSet = [](const fs::path& dir, bool complete) {
        std::error_code ec;
        fs::create_directories(dir / "data" / "minecraft", ec);
        for (const std::string& name : GameFiles::dataTableFiles()) {
            if (complete || name != "facts.json") writeFileAtomic(dir / name, "{}");
        }
    };
    CHECK(!files.findGameData("26.3").has_value());  // nothing yet
    makeSet(files.versionDir("26.3"), false);        // a table missing: not enough
    CHECK(!GameFiles::isGameDataDir(files.versionDir("26.3")) && !files.findGameData("26.3").has_value());
    makeSet(files.versionDir("26.3"), true);
    CHECK(GameFiles::isGameDataDir(files.versionDir("26.3")) && files.findGameData("26.3") == std::optional<fs::path>(files.versionDir("26.3")));
    CHECK(!files.findGameData("27.0").has_value());  // another game version is another folder
    // a set somewhere else (horizon-launcher's, say) is not looked for any more
    makeSet(root / "elsewhere" / "horizon-launcher" / "versions" / "x" / ".local" / "horizon-client" / "27.0", true);
    CHECK(!files.findGameData("27.0").has_value());
    // SHA-256, as the tables are checked with it
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" && sha256Hex("").size() == 64);
}

#ifdef _WIN32
constexpr const char* kClientName = "Falcon.exe";
#else
constexpr const char* kClientName = "Falcon";
#endif

void test_bundled_client() {
    const fs::path root = freshFolder("client");
    const std::string exe = "MZ-not-really-an-exe";
    const Blob blob{reinterpret_cast<const unsigned char*>(exe.data()), exe.size()};
    CHECK(!installBundledClient(root, Blob{}).ok() && installBundledClient(root, Blob{}).error.code == "no_client");
    fs::path where;
    CHECK(!bundledClientInstalled(root, blob, &where));
    const auto installed = installBundledClient(root, blob);
    CHECK(installed.ok() && fs::exists(*installed) && installed->filename() == kClientName);
    std::string back;
    CHECK(readFile(*installed, back) && back == exe && bundledClientInstalled(root, blob, &where) && where == *installed);
    // a damaged copy is put right; an older launcher's client folder is removed
    writeFileAtomic(*installed, "damaged");
    CHECK(!bundledClientInstalled(root, blob));
    CHECK(installBundledClient(root, blob).ok() && bundledClientInstalled(root, blob));
    const std::string oldFolder = sha256Hex("an older client").substr(0, 16);
    fs::create_directories(root / "client" / oldFolder);
    writeFileAtomic(root / "client" / oldFolder / kClientName, "old");
    fs::create_directories(root / "client" / "aebb7bb00fc1");  // a folder of a launcher that named them after the SHA-1
    writeFileAtomic(root / "client" / "aebb7bb00fc1" / kClientName, "older");
    fs::create_directories(root / "client" / "keep-me");
    writeFileAtomic(root / "client" / "keep-me" / "notes.txt", "not a client folder");
    writeFileAtomic(*installed, "damaged again");
    CHECK(installBundledClient(root, blob).ok() && !fs::exists(root / "client" / oldFolder) && !fs::exists(root / "client" / "aebb7bb00fc1"));
    // only folders named like one of our client builds are cleaned up: nothing else a player keeps in there is touched
    CHECK(fs::exists(root / "client" / "keep-me" / "notes.txt"));
    // the folder is named after the start of the client's SHA-256 (it used to be its SHA-1)
    const std::string id = installed->parent_path().filename().string();
    CHECK(id.size() == 16 && id == sha256Hex(exe).substr(0, 16));
}

void test_signature_report() {
#ifdef _WIN32
    // An unsigned file is reported as unsigned (a development build), never as signed: nothing here can invent a signature.
    const fs::path folder = freshFolder("signature");
    const fs::path file = folder / "unsigned.exe";
    writeFileAtomic(file, "MZ-not-really-an-exe");
    const FileSignature plain = verifyFileSignature(file, "Falcon project");
    CHECK(plain.state == FileSignature::State::Unsigned && plain.publisher.empty() && plain.error.empty());
    // A file that is not there cannot be verified, and is not silently accepted.
    CHECK(verifyFileSignature(folder / "missing.exe").state == FileSignature::State::Invalid);
#endif
}

// ---- the controller ------------------------------------------------------------------------------------------------------------------

struct Events {
    std::mutex mutex;
    std::vector<json> states;
    std::vector<bool> running;
    std::vector<json> answers;  // the answers that came later, through emit ({"id": ...})
    bool quit = false;
    // The stand-in for the sign-in window: how the "player" answers (ok, cancel, denied, badstate, window, hang = never), and what it was asked.
    std::string signInMode = "ok";
    std::string signInUrl, signInPrefix;
    std::atomic<int> signInCalls{0};
    std::atomic<int> cancelCalls{0};
};

AppHooks hooksFor(Events& e) {
    AppHooks h;
    h.beginSignIn = [&e](const std::string& url, const std::string& prefix, std::function<void(const std::string&, const std::string&)> done) {
        std::string mode;
        {
            std::lock_guard<std::mutex> lock(e.mutex);
            e.signInUrl = url;
            e.signInPrefix = prefix;
            mode = e.signInMode;
        }
        ++e.signInCalls;
        const std::size_t at = url.find("state=");
        const std::string state = at == std::string::npos ? std::string() : url.substr(at + 6, url.find('&', at) - (at + 6));
        if (mode == "cancel") done("", "cancelled");
        else if (mode == "window") done("", "no graphics");
        else if (mode == "denied") done(prefix + "?error=access_denied&error_description=The+user+said+no&state=" + state, "");
        else if (mode == "badstate") done(prefix + "?code=thecode&state=somebody-else", "");
        else if (mode == "ok") done(prefix + "?code=thecode&lc=1033&state=" + state, "");
        // "hang": the window stays open and never answers
    };
    h.cancelSignIn = [&e] { ++e.cancelCalls; };
    h.emit = [&e](const std::string& text) {
        const json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.value("event", "") == "state") {
            std::lock_guard<std::mutex> lock(e.mutex);
            e.states.push_back(j["state"]);
        } else if (j.is_object() && j.contains("id")) {
            std::lock_guard<std::mutex> lock(e.mutex);
            e.answers.push_back(j);
        }
    };
    h.gameRunning = [&e](bool r) {
        std::lock_guard<std::mutex> lock(e.mutex);
        e.running.push_back(r);
    };
    h.quit = [&e] { e.quit = true; };
    return h;
}

json parse(const std::string& text) { return json::parse(text, nullptr, false); }

bool waitFor(App& app, const std::function<bool(const json&)>& condition, int seconds = 10) {
    for (int i = 0; i < seconds * 50; ++i) {
        if (condition(parse(app.stateJson()))) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

void test_messages() {
    FakeHttp http;
    Events events;
    AppOptions options;
    options.mock = true;
    options.mockState = "home";
    App app(http, options, hooksFor(events), Blob{});
    CHECK(!parse(app.handleMessage("this is not json"))["ok"].get<bool>());
    CHECK(parse(app.handleMessage(R"({"id":1,"method":"nope"})"))["error"]["code"] == "unknown_method");
    CHECK(parse(app.handleMessage(R"({"id":2,"method":"openUrl","params":{"url":"http://insecure.example"}})"))["error"]["code"] == "bad_url");
    CHECK(parse(app.handleMessage(R"({"id":3,"method":"openUrl","params":{"url":"file:///C:/Windows"}})"))["error"]["code"] == "bad_url");
    CHECK(parse(app.handleMessage(R"({"id":4,"method":"openUrl"})"))["error"]["code"] == "bad_url");
    // the Logs tab and the Files card
    const json log = parse(app.handleMessage(R"({"id":9,"method":"getLog"})"));
    CHECK(log["ok"].get<bool>() && !log["result"]["text"].get<std::string>().empty());
    CHECK(!parse(app.stateJson())["app"]["gameDir"].get<std::string>().empty());
    const json answer = parse(app.handleMessage(R"({"id":"seven","method":"getState"})"));
    CHECK(answer["id"] == "seven" && answer["ok"].get<bool>() && answer["result"]["phase"] == "boot");
    // nobody is signed in yet: launching is refused before anything happens
    CHECK(parse(app.handleMessage(R"({"id":5,"method":"launch"})"))["error"]["code"] == "not_signed_in");
    CHECK(parse(app.handleMessage(R"({"id":6,"method":"setSetting","params":{"key":"nonsense","value":1}})"))["error"]["code"] == "bad_setting");
    (void)app.handleMessage(R"({"id":7,"method":"quit"})");
    CHECK(events.quit);
}

void test_mock_states() {
    _putenv_s("FALCON_LAUNCHER_HOME", freshFolder("mockhome").string().c_str());
    struct Case {
        const char* state;
        const char* phase;
    };
    const Case cases[] = {{"setup", "setup"}, {"signin", "signin"}, {"waiting", "signin"}, {"device", "signin"}, {"error", "signin"}, {"home", "home"}, {"missing", "home"}, {"notowned", "home"},
                          {"noprofile", "home"}, {"nodata", "home"}, {"installing", "home"}, {"running", "home"}};
    for (const Case& c : cases) {
        FakeHttp http;
        Events events;
        AppOptions options;
        options.mockState = c.state;
        App app(http, options, hooksFor(events), Blob{});
        (void)app.handleMessage(R"({"id":1,"method":"uiReady"})");
        const json s = parse(app.stateJson());
        CHECK(s["phase"] == c.phase);
        CHECK(s["app"]["mock"].get<bool>());
        if (std::string(c.state) == "waiting" || std::string(c.state) == "device") CHECK(s["auth"]["status"] == "waiting" && !s["auth"]["message"].get<std::string>().empty());
        if (s["phase"] == "home") CHECK(s["account"]["avatar"].get<std::string>().rfind("data:image/png;base64,", 0) == 0);
        if (std::string(c.state) == "error") CHECK(!s["auth"]["error"].is_null());
        if (std::string(c.state) == "notowned") CHECK(!s["account"]["owns"].get<bool>());
        if (std::string(c.state) == "noprofile") CHECK(!s["account"]["hasProfile"].get<bool>());
        CHECK(s["game"]["data"].get<bool>() == (std::string(c.state) != "nodata"));
        if (std::string(c.state) == "installing") CHECK(s["game"]["status"] == "working" && s["game"]["total"].get<long long>() > 0);
        if (std::string(c.state) == "running") CHECK(s["game"]["status"] == "running");
        CHECK(!events.states.empty());  // the UI is told
    }
}

void test_mock_flow() {
    _putenv_s("FALCON_LAUNCHER_MOCK_SPEED", "0.02");
    _putenv_s("FALCON_LAUNCHER_HOME", freshFolder("mockflow").string().c_str());
    FakeHttp http;
    Events events;
    AppOptions options;
    options.mock = true;
    App app(http, options, hooksFor(events), Blob{});
    (void)app.handleMessage(R"({"id":1,"method":"uiReady"})");
    CHECK(waitFor(app, [](const json& s) { return s["phase"] == "signin"; }));  // setup ran, then the account page
    CHECK(parse(app.handleMessage(R"({"id":2,"method":"signIn"})"))["ok"].get<bool>());
    CHECK(waitFor(app, [](const json& s) { return s["phase"] == "home" && s["auth"]["status"] == "signedIn"; }));
    app.waitIdle();
    CHECK(parse(app.stateJson())["account"]["owns"].get<bool>());
    CHECK(parse(app.handleMessage(R"({"id":3,"method":"launch"})"))["ok"].get<bool>());
    // "running" only lasts the scaled mock wait (a few dozen milliseconds at the test speed). A loaded
    // machine can go a whole poll interval without looking, so the states the page was sent count too.
    const auto passedThrough = [&](const char* status) {
        return waitFor(app, [&](const json& live) {
            if (live["game"]["status"] == status) return true;
            std::lock_guard<std::mutex> lock(events.mutex);
            for (const json& past : events.states) {
                if (past["game"]["status"] == status) return true;
            }
            return false;
        });
    };
    CHECK(passedThrough("running"));
    CHECK(waitFor(app, [](const json& s) { return s["game"]["status"] == "ready"; }));
    app.waitIdle();
    {
        std::lock_guard<std::mutex> lock(events.mutex);
        CHECK(events.running == std::vector<bool>({true, false}));  // the window is told to step aside, then to come back
    }
    // while busy, a second job is refused
    CHECK(parse(app.handleMessage(R"({"id":4,"method":"launch"})"))["ok"].get<bool>());
    CHECK(parse(app.handleMessage(R"({"id":5,"method":"launch"})"))["error"]["code"] == "busy");
    (void)app.handleMessage(R"({"id":6,"method":"cancelLaunch"})");
    app.waitIdle();
    // signing out
    (void)app.handleMessage(R"({"id":7,"method":"signOut"})");
    const json out = parse(app.stateJson());
    CHECK(out["phase"] == "signin" && out["account"].is_null() && out["auth"]["status"] == "signedOut");
}

// Every way the sign-in window can end, with the fake network and no setup around it.
void test_sign_in_outcomes() {
    _putenv_s("FALCON_LAUNCHER_NO_BROWSER", "1");
    const auto run = [](const char* mode, const char* name, const std::function<void(App&, Events&, FakeHttp&)>& check) {
        const fs::path home = freshFolder(name);
        _putenv_s("FALCON_LAUNCHER_HOME", home.string().c_str());
        FakeHttp http;
        http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"access_token":"ms-access","refresh_token":"ms-refresh","expires_in":3600})")});
        http.on("GET", "https://textures.minecraft.net/texture/abc123", {FakeHttp::reply(200, flatSkinPng())});
        serveXboxChain(http, true, true);
        Events events;
        events.signInMode = mode;
        AppOptions options;
        options.clientId = kDefaultMinecraftClientId;
        App app(http, options, hooksFor(events), Blob{});
        check(app, events, http);
        app.shutdown();
    };
    const auto signIn = [](App& app) { return parse(app.handleMessage(R"({"id":1,"method":"signIn"})")); };

    run("ok", "so_ok", [&](App& app, Events& events, FakeHttp& http) {
        CHECK(signIn(app)["ok"].get<bool>());
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["phase"] == "home" && s["auth"]["status"] == "signedIn" && s["auth"]["error"].is_null() && s["account"]["name"] == "Steve");
        CHECK(events.signInCalls == 1 && http.count("POST", kTokenUrl) == 1);
        CHECK(fs::exists(dataRoot() / "account.bin"));
    });
    run("cancel", "so_cancel", [&](App& app, Events&, FakeHttp& http) {  // the player closed Microsoft's window: back to the button, no error, no network
        CHECK(signIn(app)["ok"].get<bool>());
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["auth"]["status"] == "signedOut" && s["auth"]["error"].is_null() && s["phase"] != "home");
        CHECK(http.count("POST", kTokenUrl) == 0 && !fs::exists(dataRoot() / "account.bin"));
    });
    run("denied", "so_denied", [&](App& app, Events&, FakeHttp& http) {  // "No" on Microsoft's page
        CHECK(signIn(app)["ok"].get<bool>());
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["auth"]["status"] == "signedOut" && s["auth"]["error"]["code"] == "declined" && http.count("POST", kTokenUrl) == 0);
    });
    run("badstate", "so_badstate", [&](App& app, Events&, FakeHttp& http) {  // an answer that is not to our request is not trusted
        CHECK(signIn(app)["ok"].get<bool>());
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["auth"]["error"]["code"] == "bad_response" && http.count("POST", kTokenUrl) == 0 && s["phase"] != "home");
    });
    run("window", "so_window", [&](App& app, Events&, FakeHttp&) {  // the window itself could not be shown
        CHECK(signIn(app)["ok"].get<bool>());
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["auth"]["error"]["code"] == "window" && s["auth"]["error"]["message"].get<std::string>().find("no graphics") != std::string::npos);
    });
    run("hang", "so_hang", [&](App& app, Events& events, FakeHttp&) {  // the window stays open; Cancel in the launcher's own page closes it
        CHECK(signIn(app)["ok"].get<bool>());
        CHECK(waitFor(app, [](const json& s) { return s["auth"]["status"] == "waiting"; }));
        CHECK(parse(app.handleMessage(R"({"id":2,"method":"signIn"})"))["error"]["code"] == "busy");  // (no second window)
        (void)app.handleMessage(R"({"id":3,"method":"cancelSignIn"})");
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["auth"]["status"] == "signedOut" && s["auth"]["error"].is_null() && events.cancelCalls >= 1);
    });
    run("ok", "so_saved", [&](App& app, Events& events, FakeHttp& http) {  // with a saved sign-in no window opens at all
        CHECK(protectToFile(dataRoot() / "account.bin", "saved-refresh"));
        http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"access_token":"renewed","expires_in":3600})")});
        CHECK(signIn(app)["ok"].get<bool>());
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["phase"] == "home" && events.signInCalls == 0 && http.last("POST", kTokenUrl).body.find("grant_type=refresh_token") != std::string::npos);
    });
}

// The real flow (the fake network, a temporary folder): setup, sign-in, the checks, the game's files, and then the start that fails (the
// "client" here is not a program) after everything before it has been done.
void test_real_flow() {
    if (!webView2RuntimeInstalled()) {
        std::printf("  (the real-flow test is skipped: it needs the WebView2 runtime, as setup does)\n");
        return;
    }
    _putenv_s("FALCON_LAUNCHER_NO_BROWSER", "1");
    const fs::path home = freshFolder("realflow");
    _putenv_s("FALCON_LAUNCHER_HOME", home.string().c_str());
    FakeHttp http;
    http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"access_token":"ms-access","refresh_token":"ms-refresh-SECRET","expires_in":3600})")});
    http.on("GET", "https://textures.minecraft.net/texture/abc123", {FakeHttp::reply(200, flatSkinPng())});
    serveXboxChain(http, true, true);
    const FakeMojang mojang = buildMojang(http, "26.3");
    const std::string exe = "not a program";
    Events events;
    AppOptions options;
    options.tables = mojang.tables;
    options.clientId = kDefaultMinecraftClientId;
    App app(http, options, hooksFor(events), Blob{reinterpret_cast<const unsigned char*>(exe.data()), exe.size()});
    (void)app.handleMessage(R"({"id":1,"method":"uiReady"})");

    // first run: the setup, with every step reported, then the account page
    CHECK(waitFor(app, [](const json& s) { return s["phase"] == "signin"; }));
    json s = parse(app.stateJson());
    CHECK(s["setup"]["steps"].size() == 5);
    for (const json& step : s["setup"]["steps"]) CHECK(step["status"] == "done" || step["status"] == "skipped");
    CHECK(s["auth"]["status"] == "signedOut" && s["auth"]["configured"].get<bool>());
    CHECK(fs::exists(home / "launcher.json") && fs::exists(home / "client") && fs::exists(home / "game"));
    CHECK(bundledClientInstalled(home, Blob{reinterpret_cast<const unsigned char*>(exe.data()), exe.size()}));

    // sign in: Microsoft's page is opened (the stand-in window answers with the redirect), the code is traded, the account is checked
    CHECK(parse(app.handleMessage(R"({"id":2,"method":"signIn"})"))["ok"].get<bool>());
    CHECK(waitFor(app, [](const json& st) { return st["phase"] == "home"; }, 15));
    app.waitIdle();
    CHECK(events.signInCalls == 1 && events.signInPrefix == kRedirectUri && events.signInUrl.find("client_id=00000000402b5328") != std::string::npos);
    {
        std::lock_guard<std::mutex> lock(events.mutex);
        bool sawWaiting = false;
        for (const json& st : events.states) sawWaiting = sawWaiting || st["auth"]["status"] == "waiting";
        CHECK(sawWaiting);  // the UI was told to show "finish in the Microsoft window"
    }
    CHECK(http.last("POST", kTokenUrl).body.find("grant_type=authorization_code") != std::string::npos && http.last("POST", kTokenUrl).body.find("code=thecode") != std::string::npos);
    s = parse(app.stateJson());
    CHECK(s["account"]["name"] == "Steve" && s["account"]["owns"].get<bool>() && s["account"]["hasProfile"].get<bool>() && s["game"]["status"] == "missing");
    CHECK(s["account"]["avatar"].get<std::string>().rfind("data:image/png;base64,", 0) == 0);  // the active skin was fetched and cut into a head
    CHECK(http.count("GET", "https://textures.minecraft.net/texture/abc123") == 1 && http.count("GET", "https://textures.minecraft.net/texture/old") == 0);
    std::string saved;
#ifdef _WIN32
    CHECK(readFile(home / "account.bin", saved) && saved.find("ms-refresh-SECRET") == std::string::npos);  // the token is not on disk in the clear
#else
    // (Linux and a Mac: in a file only this user can read, see secure_store.cpp, holding the sign-in as the launcher keeps it: the refresh token and the Minecraft one)
    CHECK(readFile(home / "account.bin", saved));
    const json signIn = parse(saved);
    CHECK(signIn.is_object() && signIn["refresh"] == "ms-refresh-SECRET");
    struct stat info{};
    CHECK(stat((home / "account.bin").c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
#endif

    // launch: the game's files come from Mojang, then the start fails (a fake exe) with a clear error and the state is sane again
    CHECK(parse(app.handleMessage(R"({"id":3,"method":"launch"})"))["ok"].get<bool>());
    app.waitIdle();
    s = parse(app.stateJson());
    CHECK(fs::exists(home / "game" / "26.3" / "installed.json") && fs::exists(home / "game" / "26.3" / "assets" / "minecraft" / "sounds" / "mob" / "cow" / "say1.ogg"));
    CHECK(s["game"]["error"]["code"] == "spawn" && s["game"]["status"] != "working" && s["game"]["status"] != "running");

    // the next start, with the saved sign-in: no code, straight to the account
    app.shutdown();
    Events events2;
    App again(http, options, hooksFor(events2), Blob{reinterpret_cast<const unsigned char*>(exe.data()), exe.size()});
    http.on("POST", kTokenUrl, {FakeHttp::reply(200, R"({"access_token":"ms-access-2","expires_in":3600})")});
    (void)again.handleMessage(R"({"id":1,"method":"uiReady"})");
    CHECK(waitFor(again, [](const json& st) { return st["phase"] == "home" && st["auth"]["status"] == "signedIn"; }, 15));
    again.waitIdle();
    CHECK(parse(again.stateJson())["account"]["name"] == "Steve");
    CHECK(http.last("POST", kTokenUrl).body.find("grant_type=refresh_token") != std::string::npos);

    // a refresh token that has been revoked: back to signed out, the saved sign-in is deleted
    again.shutdown();
    FakeHttp revoked;
    revoked.on("POST", kTokenUrl, {FakeHttp::reply(400, R"({"error":"invalid_grant"})")});
    Events events3;
    App third(revoked, options, hooksFor(events3), Blob{reinterpret_cast<const unsigned char*>(exe.data()), exe.size()});
    (void)third.handleMessage(R"({"id":1,"method":"uiReady"})");
    third.waitIdle();
    s = parse(third.stateJson());
    CHECK(s["phase"] == "signin" && s["auth"]["status"] == "signedOut" && !fs::exists(home / "account.bin"));
}

void test_not_configured_and_errors() {
    _putenv_s("FALCON_LAUNCHER_NO_BROWSER", "1");
    _putenv_s("FALCON_LAUNCHER_HOME", freshFolder("noclient").string().c_str());
    FakeHttp http;
    const std::string exe = "x";
    Events events;
    AppOptions options;  // no client id
    App app(http, options, hooksFor(events), Blob{reinterpret_cast<const unsigned char*>(exe.data()), exe.size()});
    (void)app.handleMessage(R"({"id":1,"method":"uiReady"})");
    if (!webView2RuntimeInstalled()) return;
    CHECK(waitFor(app, [](const json& s) { return s["phase"] == "signin"; }));
    (void)app.handleMessage(R"({"id":2,"method":"signIn"})");
    CHECK(waitFor(app, [](const json& s) { return !s["auth"]["error"].is_null(); }));
    app.waitIdle();
    const json s = parse(app.stateJson());
    CHECK(s["auth"]["error"]["code"] == "not_configured" && !s["auth"]["configured"].get<bool>());
}

}  // namespace

// ---- the Mods tab (Modrinth) ---------------------------------------------------------------------------------------------------------

const char* const kSearchReply = R"({"hits":[
  {"project_id":"AANobbMI","slug":"lumen","title":"Lumen","description":"A rendering engine replacement.","author":"jelly","icon_url":"https://cdn.modrinth.com/data/AANobbMI/icon.png","downloads":253751616,"display_categories":["fabric","utility"],"categories":["fabric"]},
  {"project_id":"bad id!","title":"Not a project id"},
  {"project_id":"P7dR8mSH","slug":"fabric-api","title":"Fabric API","description":"The library.","author":"modmuss","icon_url":"https://evil.example/icon.png","downloads":1,"categories":["fabric","library"]}
],"offset":0,"limit":20,"total_hits":2})";

std::string versionsReply(const std::string& url, const std::string& sha1, std::size_t size, const std::string& filename = "lumen-fabric-0.5.0+mc26.3.jar") {
    return json::array({json{{"id", "BETA0001"}, {"version_number", "0.6.0-beta"}, {"version_type", "beta"}, {"loaders", json::array({"fabric"})},
                             {"files", json::array({json{{"filename", "beta.jar"}, {"primary", true}, {"url", "https://cdn.modrinth.com/data/AANobbMI/versions/BETA0001/beta.jar"}, {"size", 5}, {"hashes", {{"sha1", std::string(40, 'a')}}}}})}},
                        json{{"id", "REL00001"}, {"version_number", "0.5.0"}, {"version_type", "release"}, {"loaders", json::array({"fabric"})},
                             {"files", json::array({json{{"filename", "sources.zip"}, {"primary", false}, {"url", "https://cdn.modrinth.com/data/AANobbMI/versions/REL00001/sources.zip"}, {"size", 9}, {"hashes", {{"sha1", std::string(40, 'b')}}}},
                                                    json{{"filename", filename}, {"primary", true}, {"url", url}, {"size", size}, {"hashes", {{"sha1", sha1}}}}})}}})
        .dump();
}

void test_mods_parts() {
    CHECK(Modrinth::isModrinthFileUrl("https://cdn.modrinth.com/data/a/versions/b/c.jar"));
    CHECK(Modrinth::isModrinthFileUrl("https://cdn-raw.modrinth.com/x/y.jar"));
    CHECK(!Modrinth::isModrinthFileUrl("http://cdn.modrinth.com/x.jar"));                    // no https
    CHECK(!Modrinth::isModrinthFileUrl("https://cdn.modrinth.com.evil.example/x.jar"));      // another host
    CHECK(!Modrinth::isModrinthFileUrl("https://cdn.modrinth.com@evil.example/x.jar"));      // a login part
    CHECK(!Modrinth::isModrinthFileUrl("https://cdn.modrinth.com:8443/x.jar"));              // a port
    CHECK(!Modrinth::isModrinthFileUrl("https://evil.example/cdn.modrinth.com/x.jar"));
    CHECK(!Modrinth::isModrinthFileUrl("https://cdn.modrinth.com"));
    CHECK(!Modrinth::isModrinthFileUrl("https://cdn.modrinth.com/a b.jar"));

    CHECK(Modrinth::isProjectId("AANobbMI") && Modrinth::isProjectId("fabric-api") && Modrinth::isProjectId("a_b"));
    CHECK(!Modrinth::isProjectId("") && !Modrinth::isProjectId("../x") && !Modrinth::isProjectId("a/b") && !Modrinth::isProjectId("a b") && !Modrinth::isProjectId(std::string(65, 'a')));

    CHECK(Modrinth::safeFileName("lumen-fabric-0.5.0+mc26.3.jar") == "lumen-fabric-0.5.0+mc26.3.jar");
    CHECK(Modrinth::safeFileName("../../Windows/evil.jar") == "evil.jar");
    CHECK(Modrinth::safeFileName("C:\\Users\\x\\evil.JAR") == "evil.JAR");
    CHECK(Modrinth::safeFileName("not a jar.zip").empty());
    CHECK(Modrinth::safeFileName(".jar").empty() && Modrinth::safeFileName("").empty() && Modrinth::safeFileName("..").empty());
    CHECK(Modrinth::safeFileName("a<b>:c|d?.jar") == "a_b__c_d_.jar");
    CHECK(Modrinth::safeFileName("CON.jar") == "_CON.jar");
    CHECK(Modrinth::safeFileName("mod.jar ") == "mod.jar");
    CHECK(Modrinth::safeFileName(std::string(300, 'a') + ".jar").size() <= 150);

    CHECK(Modrinth::imageMime(std::string("\x89PNG\r\n\x1a\n....", 12)) == "image/png");
    CHECK(Modrinth::imageMime("\xFF\xD8\xFF\xE0....") == "image/jpeg");
    CHECK(Modrinth::imageMime("GIF89a....") == "image/gif");
    CHECK(Modrinth::imageMime(std::string("RIFF\x10\0\0\0WEBPVP8 ", 16)) == "image/webp");
    CHECK(Modrinth::imageMime("<svg xmlns='http://www.w3.org/2000/svg'></svg>") == "image/svg+xml");
    CHECK(Modrinth::imageMime("<?xml version='1.0'?><svg></svg>") == "image/svg+xml");
    CHECK(Modrinth::imageMime("<html><script>alert(1)</script>").empty());
    CHECK(Modrinth::imageMime("MZ\x90\0 an executable").empty() && Modrinth::imageMime("").empty());

    FakeHttp http;
    Modrinth api(http, "https://api.modrinth.com/");
    ModQuery q;
    q.text = "lumen & co";
    q.gameVersion = "26.3";
    q.loader = "fabric";
    q.offset = 40;
    const std::string url = api.searchUrl(q);
    CHECK(url.rfind("https://api.modrinth.com/v2/search?", 0) == 0);  // (the slash at the end of the base is dropped)
    CHECK(url.find("query=lumen%20%26%20co") != std::string::npos || url.find("query=lumen+%26+co") != std::string::npos);
    CHECK(url.find("project_type%3Amod") != std::string::npos && url.find("versions%3A26.3") != std::string::npos && url.find("categories%3Afabric") != std::string::npos);
    CHECK(url.find("offset=40") != std::string::npos && url.find("index=relevance") != std::string::npos);
    ModQuery empty;
    empty.gameVersion = "26.3";
    CHECK(api.searchUrl(empty).find("index=downloads") != std::string::npos);
    ModQuery odd;
    odd.gameVersion = "26.3\"],[\"x";  // not a game version: it is left out rather than put in the facets
    odd.loader = "pirate";
    CHECK(api.searchUrl(odd).find("versions") == std::string::npos && api.searchUrl(odd).find("pirate") == std::string::npos);
    CHECK(api.versionsUrl("AANobbMI", "26.3", "").find("/v2/project/AANobbMI/version?game_versions=") != std::string::npos);

    // the search answer: a hit without a usable id is skipped, the fields are what they were
    const auto page = Modrinth::parseSearch(kSearchReply);
    CHECK(page.ok() && page->hits.size() == 2 && page->total == 2 && page->limit == 20);
    if (page.ok() && page->hits.size() == 2) {
        CHECK(page->hits[0].projectId == "AANobbMI" && page->hits[0].title == "Lumen" && page->hits[0].downloads == 253751616 && page->hits[0].categories.size() == 2);
        CHECK(page->hits[1].categories[1] == "library");
    }
    CHECK(!Modrinth::parseSearch("not json").ok() && !Modrinth::parseSearch("{}").ok() && !Modrinth::parseSearch("[]").ok());

    // the file to take: the newest RELEASE (not the beta that is listed first), its primary .jar (not the sources)
    const std::string good = "https://cdn.modrinth.com/data/AANobbMI/versions/REL00001/lumen.jar";
    const auto file = Modrinth::pickFile(versionsReply(good, std::string(40, 'c'), 123), "AANobbMI");
    CHECK(file.ok() && file->versionNumber == "0.5.0" && file->filename == "lumen-fabric-0.5.0+mc26.3.jar" && file->url == good && file->size == 123 && file->loader == "fabric");
    // a version whose only file is from another host or has no checksum is skipped, and with nothing left there is no file
    CHECK(Modrinth::pickFile(versionsReply("https://evil.example/x.jar", std::string(40, 'c'), 1), "AANobbMI")->versionNumber == "0.6.0-beta");  // (the beta's file is fine)
    CHECK(Modrinth::pickFile(R"([{"version_type":"release","files":[{"filename":"a.jar","url":"https://evil.example/a.jar","hashes":{"sha1":"0000000000000000000000000000000000000000"}}]}])", "x").error.code == "no_file");
    CHECK(Modrinth::pickFile(R"([{"version_type":"release","files":[{"filename":"a.jar","url":"https://cdn.modrinth.com/a.jar","hashes":{}}]}])", "x").error.code == "no_file");
    CHECK(Modrinth::pickFile("[]", "x").error.code == "no_file" && Modrinth::pickFile("oops", "x").error.code == "bad_reply");
}

void test_mods_network() {
    const fs::path dir = freshFolder("mods");
    const std::string jar = std::string("PK\x03\x04 pretend this is a mod") + std::string(200, 'x');
    const std::string jarUrl = "https://cdn.modrinth.com/data/AANobbMI/versions/REL00001/lumen.jar";
    FakeHttp http;
    Modrinth api(http);

    ModQuery q;
    q.gameVersion = "26.3";
    http.on("GET", api.searchUrl(q), {FakeHttp::reply(200, kSearchReply)});
    const auto page = api.search(q);
    CHECK(page.ok() && page->hits.size() == 2);
    ModQuery down;
    down.text = "down";
    http.on("GET", api.searchUrl(down), {FakeHttp::unreachable()});
    CHECK(api.search(down).error.code == "network");
    ModQuery limited;
    limited.text = "limited";
    http.on("GET", api.searchUrl(limited), {FakeHttp::reply(429, "{}")});
    CHECK(api.search(limited).error.code == "rate_limited");

    // icons: a real picture becomes a data address, a page that is not a picture and a host that is not Modrinth's are left out, and a second ask is answered from memory
    const std::string png("\x89PNG\r\n\x1a\n and some more", 20);
    http.on("GET", "https://cdn.modrinth.com/data/AANobbMI/icon.png", {FakeHttp::reply(200, png)});
    http.on("GET", "https://cdn.modrinth.com/data/P7dR8mSH/icon.png", {FakeHttp::reply(200, "<html>not a picture</html>")});
    const std::vector<std::string> urls = {"https://cdn.modrinth.com/data/AANobbMI/icon.png", "https://evil.example/icon.png", "https://cdn.modrinth.com/data/P7dR8mSH/icon.png", ""};
    const auto icons = api.icons(urls);
    CHECK(icons.size() == 4 && icons[0].rfind("data:image/png;base64,", 0) == 0 && icons[1].empty() && icons[2].empty() && icons[3].empty());
    CHECK(http.count("GET", "https://evil.example/icon.png") == 0);
    (void)api.icons(urls);
    CHECK(http.count("GET", "https://cdn.modrinth.com/data/AANobbMI/icon.png") == 1);

    // installing: the right version is chosen, the file is checked, and it lands in the mods folder
    http.on("GET", api.versionsUrl("AANobbMI", "26.3", ""), {FakeHttp::reply(200, versionsReply(jarUrl, sha1Hex(jar), jar.size()))});
    http.on("GET", jarUrl, {FakeHttp::reply(200, jar)});
    const auto chosen = api.chooseFile("AANobbMI", "26.3", "");
    CHECK(chosen.ok());
    std::atomic<bool> cancel{false};
    const auto done = api.install(*chosen, "Lumen", dir / "mods", cancel);
    CHECK(done.ok() && done->file == "lumen-fabric-0.5.0+mc26.3.jar" && done->versionNumber == "0.5.0");
    std::string written;
    CHECK(readFile(dir / "mods" / "lumen-fabric-0.5.0+mc26.3.jar", written) && written == jar);
    int leftovers = 0;
    for (const auto& entry : fs::directory_iterator(dir / "mods")) leftovers += entry.path().extension() == ".part" ? 1 : 0;
    CHECK(leftovers == 0);

    // a file that is not the one Modrinth named (corrupted, or swapped) is not kept, and nothing is left behind
    ModFile bad = *chosen;
    bad.sha1 = std::string(40, 'f');
    bad.filename = "other.jar";
    CHECK(api.install(bad, "Other", dir / "mods", cancel).error.code == "hash_mismatch");
    CHECK(!fs::exists(dir / "mods" / "other.jar"));
    ModFile wrongSize = *chosen;
    wrongSize.size = jar.size() + 1;
    wrongSize.filename = "size.jar";
    CHECK(api.install(wrongSize, "Size", dir / "mods", cancel).error.code == "bad_file" && !fs::exists(dir / "mods" / "size.jar"));
    // a name that tries to leave the folder is cut down to its last part; an address that is not Modrinth's is refused before anything is asked of it
    ModFile sneaky = *chosen;
    sneaky.filename = "../../escape.jar";
    CHECK(api.install(sneaky, "Sneaky", dir / "mods", cancel).ok() && fs::exists(dir / "mods" / "escape.jar") && !fs::exists(dir / "escape.jar"));
    ModFile foreign = *chosen;
    foreign.url = "https://evil.example/x.jar";
    CHECK(api.install(foreign, "Foreign", dir / "mods", cancel).error.code == "bad_file" && http.count("DOWNLOAD", "https://evil.example/x.jar") == 0);
    ModFile notJar = *chosen;
    notJar.filename = "readme.txt";
    CHECK(api.install(notJar, "Text", dir / "mods", cancel).error.code == "bad_file");
    // a download that fails says why
    ModFile missing = *chosen;
    missing.url = "https://cdn.modrinth.com/data/none.jar";
    missing.filename = "none.jar";
    CHECK(api.install(missing, "None", dir / "mods", cancel).error.code == "http" && !fs::exists(dir / "mods" / "none.jar"));
}

// The messages: the answers of searchMods and installMod come through hooks.emit, not as the return value.
json answerOf(Events& events, int id) {
    for (int waited = 0; waited < 500; ++waited) {
        {
            std::lock_guard<std::mutex> lock(events.mutex);
            for (const json& a : events.answers) {
                if (a["id"] == id) return a;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return json::object();
}

void test_mods_messages() {
    _putenv_s("FALCON_LAUNCHER_HOME", freshFolder("modshome").string().c_str());
    _putenv_s("FALCON_LAUNCHER_MOCK_SPEED", "0.02");
    {  // the demo mode: made-up mods, no network and no disk
        FakeHttp http;
        Events events;
        AppOptions options;
        options.mockState = "home";
        App app(http, options, hooksFor(events), Blob{});
        (void)app.handleMessage(R"({"id":1,"method":"uiReady"})");
        CHECK(app.handleMessage(R"({"id":10,"method":"searchMods","params":{"query":"","loader":"","offset":0}})").empty());  // (answered later)
        const json all = answerOf(events, 10);
        CHECK(all["ok"].get<bool>() && all["result"]["hits"].size() == 7 && all["result"]["total"] == 7 && all["result"]["hits"][0]["icon"].get<std::string>().rfind("data:image/svg+xml;base64,", 0) == 0);
        (void)app.handleMessage(R"({"id":11,"method":"searchMods","params":{"query":"zoom","loader":"","offset":0}})");
        const json zoom = answerOf(events, 11);
        CHECK(zoom["result"]["hits"].size() == 1 && zoom["result"]["hits"][0]["title"] == "Sample Zoom");
        (void)app.handleMessage(R"({"id":12,"method":"searchMods","params":{"query":"","loader":"quilt","offset":0}})");
        CHECK(answerOf(events, 12)["result"]["total"] == 2);
        // installing: busy at once, installed when it is done (in the state the page draws), and not twice at the same time
        CHECK(app.handleMessage(R"({"id":13,"method":"installMod","params":{"projectId":"mock0003","title":"Sample Zoom","loader":""}})").empty());
        CHECK(parse(app.stateJson())["mods"]["busy"].contains("mock0003"));
        CHECK(parse(app.handleMessage(R"({"id":14,"method":"installMod","params":{"projectId":"mock0003"}})"))["error"]["code"] == "busy");
        const json installed = answerOf(events, 13);
        CHECK(installed["ok"].get<bool>() && installed["result"]["projectId"] == "mock0003");
        app.waitIdle();
        const json s = parse(app.stateJson());
        CHECK(s["mods"]["busy"].empty() && s["mods"]["installed"]["mock0003"]["title"] == "Sample Zoom" && !s["mods"]["dir"].get<std::string>().empty());
        CHECK(parse(app.handleMessage(R"({"id":15,"method":"installMod","params":{"projectId":"../evil"}})"))["error"]["code"] == "bad_project");
        CHECK(parse(app.handleMessage(R"({"id":16,"method":"installMod"})"))["error"]["code"] == "bad_project");
    }
    {  // the real thing, against the fake network
        const fs::path home = freshFolder("modsreal");
        _putenv_s("FALCON_LAUNCHER_HOME", home.string().c_str());
        const std::string jar = std::string("PK\x03\x04 the mod") + std::string(64, 'y');
        const std::string jarUrl = "https://cdn.modrinth.com/data/AANobbMI/versions/REL00001/lumen.jar";
        FakeHttp http;
        Modrinth api(http);
        ModQuery q;
        q.gameVersion = "26.3";
        http.on("GET", api.searchUrl(q), {FakeHttp::reply(200, kSearchReply)});
        http.on("GET", "https://cdn.modrinth.com/data/AANobbMI/icon.png", {FakeHttp::reply(200, std::string("\x89PNG\r\n\x1a\n and some more", 20))});
        http.on("GET", api.versionsUrl("AANobbMI", "26.3", ""), {FakeHttp::reply(200, versionsReply(jarUrl, sha1Hex(jar), jar.size()))});
        http.on("GET", jarUrl, {FakeHttp::reply(200, jar)});
        http.on("GET", "https://api.modrinth.com/v2/project/AANobbMI", {FakeHttp::reply(200, R"({"id":"AANobbMI","slug":"lumen","title":"Lumen","categories":["fabric","utility"]})")});
        http.on("GET", "https://api.modrinth.com/v2/project/P7dR8mSH", {FakeHttp::reply(200, R"({"id":"P7dR8mSH","slug":"fabric-api","title":"Fabric API","categories":["fabric","library"]})")});
        Events events;
        AppOptions options;
        App app(http, options, hooksFor(events), Blob{});
        CHECK(app.handleMessage(R"({"id":20,"method":"searchMods","params":{"query":""}})").empty());
        const json found = answerOf(events, 20);
        CHECK(found["ok"].get<bool>() && found["result"]["hits"].size() == 2 && found["result"]["hits"][0]["title"] == "Lumen" && found["result"]["hits"][0]["icon"].get<std::string>().rfind("data:image/png", 0) == 0);
        CHECK(found["result"]["hits"][1]["icon"].get<std::string>().empty());  // (a picture on a host that is not Modrinth's is not fetched)
        // the search fails: the page is told why
        (void)app.handleMessage(R"({"id":21,"method":"searchMods","params":{"query":"nothing here"}})");
        const json failed = answerOf(events, 21);
        CHECK(!failed["ok"].get<bool>() && failed["error"]["code"] == "http");
        // installing: the file is in the mods folder, noted in mods.json, and still noted after a restart of the launcher
        (void)app.handleMessage(R"({"id":22,"method":"installMod","params":{"projectId":"AANobbMI","title":"Lumen","loader":""}})");
        const json done = answerOf(events, 22);
        CHECK(done["ok"].get<bool>() && done["result"]["file"] == "lumen-fabric-0.5.0+mc26.3.jar");
        app.waitIdle();
        CHECK(fs::exists(home / "mods" / "lumen-fabric-0.5.0+mc26.3.jar"));
        CHECK(parse(app.stateJson())["mods"]["installed"]["AANobbMI"]["version"] == "0.5.0");
        {  // (a search also looks at the mods folder, which is the truth: so a restarted launcher knows what is installed, and a mod deleted by hand stops showing)
            Events again;
            App restarted(http, options, hooksFor(again), Blob{});
            (void)restarted.handleMessage(R"({"id":30,"method":"searchMods","params":{"query":""}})");
            CHECK(answerOf(again, 30)["ok"].get<bool>());
            restarted.waitIdle();
            CHECK(parse(restarted.stateJson())["mods"]["installed"].contains("AANobbMI"));
            fs::remove(home / "mods" / "lumen-fabric-0.5.0+mc26.3.jar");
            (void)restarted.handleMessage(R"({"id":31,"method":"searchMods","params":{"query":""}})");
            CHECK(answerOf(again, 31)["ok"].get<bool>());
            restarted.waitIdle();
            CHECK(!parse(restarted.stateJson())["mods"]["installed"].contains("AANobbMI"));
        }
        // a mod whose version list has no file for this game version: said plainly, nothing installed
        http.on("GET", api.versionsUrl("P7dR8mSH", "26.3", ""), {FakeHttp::reply(200, "[]")});
        (void)app.handleMessage(R"({"id":23,"method":"installMod","params":{"projectId":"P7dR8mSH","title":"Fabric API"}})");
        const json none = answerOf(events, 23);
        CHECK(!none["ok"].get<bool>() && none["error"]["code"] == "no_file");
        app.waitIdle();
        CHECK(parse(app.stateJson())["mods"]["busy"].empty());
    }
    _putenv_s("FALCON_LAUNCHER_MOCK_SPEED", "1");
}

// A zip of stored entries (what a mod's .jar is, for the reader of the mod's own metadata; it does not check the CRC).
std::string storedZip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out;
    const auto le16 = [&](std::string& to, unsigned v) { to.push_back(static_cast<char>(v & 0xFF)), to.push_back(static_cast<char>((v >> 8) & 0xFF)); };
    const auto le32 = [&](std::string& to, unsigned v) { le16(to, v & 0xFFFF), le16(to, v >> 16); };
    std::vector<unsigned> offsets;
    for (const auto& [name, data] : files) {
        offsets.push_back(static_cast<unsigned>(out.size()));
        le32(out, 0x04034b50);
        le16(out, 20), le16(out, 0), le16(out, 0), le16(out, 0), le16(out, 0), le32(out, 0);
        le32(out, static_cast<unsigned>(data.size())), le32(out, static_cast<unsigned>(data.size()));
        le16(out, static_cast<unsigned>(name.size())), le16(out, 0);
        out += name + data;
    }
    const unsigned centralStart = static_cast<unsigned>(out.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& [name, data] = files[i];
        le32(out, 0x02014b50);
        le16(out, 20), le16(out, 20), le16(out, 0), le16(out, 0), le16(out, 0), le16(out, 0), le32(out, 0);
        le32(out, static_cast<unsigned>(data.size())), le32(out, static_cast<unsigned>(data.size()));
        le16(out, static_cast<unsigned>(name.size())), le16(out, 0), le16(out, 0), le16(out, 0), le16(out, 0), le32(out, 0), le32(out, offsets[i]);
        out += name;
    }
    const unsigned centralSize = static_cast<unsigned>(out.size()) - centralStart;
    le32(out, 0x06054b50);
    le16(out, 0), le16(out, 0), le16(out, static_cast<unsigned>(files.size())), le16(out, static_cast<unsigned>(files.size())), le32(out, centralSize), le32(out, centralStart), le16(out, 0);
    return out;
}

// Optimization mods (Sodium and the like) are not installed, however they are asked for: marked in the search, refused before the download, refused after it by what the jar says.
void test_mods_blocker() {
    const std::string reply = R"({"hits":[
      {"project_id":"AANobbMI","slug":"sodium","title":"Sodium","description":"d","author":"a","categories":["fabric"]},
      {"project_id":"NEWSPEED","slug":"brand-new-speedup","title":"Brand New Speedup","description":"d","author":"a","display_categories":["fabric"],"categories":["fabric","optimization"]},
      {"project_id":"P7dR8mSH","slug":"fabric-api","title":"Fabric API","description":"d","author":"a","categories":["fabric","library"]}
    ],"offset":0,"limit":20,"total_hits":3})";
    const auto page = Modrinth::parseSearch(reply);
    CHECK(page.ok() && page->hits.size() == 3);
    if (page.ok() && page->hits.size() == 3) {
        CHECK(page->hits[0].blocked && page->hits[0].blockedReason.find("never will be") != std::string::npos);                      // a known one, by its slug
        CHECK(page->hits[1].blocked && page->hits[1].blockedReason.find("Optimization on Modrinth") != std::string::npos);           // one nobody listed: Modrinth's category
        CHECK(!page->hits[2].blocked && page->hits[2].blockedReason.empty());
    }
    // a project's page, and a list of them: every category counts, the extra ones too
    const auto projects = Modrinth::parseProjects(R"([{"id":"X1","slug":"shiny","title":"Shiny","categories":["decoration"],"additional_categories":["optimization"]},{"id":"X2","slug":"modmenu","title":"Mod Menu","categories":["utility"]}])");
    CHECK(projects.ok() && projects->size() == 2 && (*projects)[0].categories.size() == 2 && (*projects)[1].slug == "modmenu");
    CHECK(Modrinth::parseProjects(R"({"id":"X3","slug":"one","title":"One","categories":[]})").ok() && !Modrinth::parseProjects("nonsense").ok());
    // a version says which projects it needs; only the required ones count
    const std::string needed = json::array({json{{"id", "REL00001"}, {"version_number", "1"}, {"version_type", "release"}, {"loaders", json::array({"fabric"})},
                                                  {"dependencies", json::array({json{{"project_id", "AANobbMI"}, {"dependency_type", "required"}}, json{{"project_id", "OPTIONAL"}, {"dependency_type", "optional"}}, json{{"project_id", "bad id!"}, {"dependency_type", "required"}}})},
                                                  {"files", json::array({json{{"filename", "a.jar"}, {"primary", true}, {"url", "https://cdn.modrinth.com/data/X/a.jar"}, {"size", 5}, {"hashes", {{"sha1", std::string(40, 'c')}}}}})}}})
                                .dump();
    const auto picked = Modrinth::pickFile(needed, "SHADERIS");
    CHECK(picked.ok() && picked->requiredProjects == std::vector<std::string>{"AANobbMI"});

    // the check against the network: a known one, one by category, a harmless one, and no answer (nothing is installed that was not checked)
    const fs::path dir = freshFolder("modsblock");
    FakeHttp http;
    Modrinth api(http);
    http.on("GET", "https://api.modrinth.com/v2/project/AANobbMI", {FakeHttp::reply(200, R"({"id":"AANobbMI","slug":"sodium","title":"Sodium","categories":["fabric"]})")});
    http.on("GET", "https://api.modrinth.com/v2/project/NEWSPEED", {FakeHttp::reply(200, R"({"id":"NEWSPEED","slug":"brand-new-speedup","title":"Brand New Speedup","categories":["fabric","optimization"]})")});
    http.on("GET", "https://api.modrinth.com/v2/project/FINE0001", {FakeHttp::reply(200, R"({"id":"FINE0001","slug":"modmenu","title":"Mod Menu","categories":["fabric","utility"]})")});
    http.on("GET", "https://api.modrinth.com/v2/project/DOWN0001", {FakeHttp::unreachable()});
    const auto known = api.vetProject("AANobbMI");
    CHECK(known.ok() && known->blocked);
    const auto filed = api.vetProject("NEWSPEED");
    CHECK(filed.ok() && filed->blocked);
    const auto fine = api.vetProject("FINE0001");
    CHECK(fine.ok() && !fine->blocked);
    CHECK(api.vetProject("DOWN0001").error.code == "network" && api.vetProject("../evil").error.code == "bad_project");

    // what a version needs: Iris-like mods need Sodium; one that needs only a harmless library is fine; none needed asks nothing
    const std::string list = "https://api.modrinth.com/v2/projects?" + formEncode({{"ids", R"(["AANobbMI","FINE0001"])"}});
    http.on("GET", list, {FakeHttp::reply(200, R"([{"id":"AANobbMI","slug":"sodium","title":"Sodium","categories":["fabric"]},{"id":"FINE0001","slug":"modmenu","title":"Mod Menu","categories":["utility"]}])")});
    ModFile shaderish;
    shaderish.requiredProjects = {"AANobbMI", "FINE0001"};
    const auto needs = api.vetDependencies(shaderish, "Shaderish");
    CHECK(needs.ok() && needs->blocked && needs->reason.find("Shaderish needs Sodium") != std::string::npos);
    CHECK(api.vetDependencies(ModFile{}, "Nothing").ok() && !api.vetDependencies(ModFile{}, "Nothing")->blocked);

    // the jar itself: a blocked mod under another name is refused after the download and nothing stays; a jar that needs one too; a harmless one is kept
    std::atomic<bool> cancel{false};
    const auto download = [&](const std::string& jar, const std::string& name) {
        const std::string url = "https://cdn.modrinth.com/data/RENAMED1/versions/V1/" + name;
        http.on("GET", url, {FakeHttp::reply(200, jar)});
        ModFile file;
        file.projectId = "RENAMED1";
        file.url = url;
        file.sha1 = sha1Hex(jar);
        file.filename = name;
        file.size = jar.size();
        return api.install(file, "Some Mod", dir / "mods", cancel);
    };
    const auto renamed = download(storedZip({{"fabric.mod.json", R"({"schemaVersion":1,"id":"sodium","version":"1","name":"Sodium"})"}}), "totally-different.jar");
    CHECK(!renamed.ok() && renamed.error.code == "optimization_mod" && !fs::exists(dir / "mods" / "totally-different.jar"));
    const auto dependent = download(storedZip({{"fabric.mod.json", R"({"schemaVersion":1,"id":"shaderish","version":"1","depends":{"sodium":">=0.5","fabricloader":"*"}})"}}), "shaderish.jar");
    CHECK(!dependent.ok() && dependent.error.code == "optimization_mod" && dependent.error.message.find("needs sodium") != std::string::npos && !fs::exists(dir / "mods" / "shaderish.jar"));
    const auto harmless = download(storedZip({{"fabric.mod.json", R"({"schemaVersion":1,"id":"modmenu","version":"1","name":"Mod Menu"})"}}), "modmenu.jar");
    CHECK(harmless.ok() && fs::exists(dir / "mods" / "modmenu.jar"));
    const auto library = download(storedZip({{"com/example/Thing.class", "x"}}), "library.jar");  // (no mod information: nothing to judge by)
    CHECK(library.ok());
    int leftovers = 0;
    for (const auto& entry : fs::directory_iterator(dir / "mods")) leftovers += entry.path().extension() == ".part" ? 1 : 0;
    CHECK(leftovers == 0);

    // through the app: the search says which are blocked, the install is refused with its reason (nothing downloaded), and the mods folder is looked at too
    const fs::path home = freshFolder("modsblockapp");
    _putenv_s("FALCON_LAUNCHER_HOME", home.string().c_str());
    FakeHttp appHttp;
    Modrinth appApi(appHttp);
    ModQuery q;
    q.gameVersion = "26.3";
    appHttp.on("GET", appApi.searchUrl(q), {FakeHttp::reply(200, reply)});
    appHttp.on("GET", "https://api.modrinth.com/v2/project/AANobbMI", {FakeHttp::reply(200, R"({"id":"AANobbMI","slug":"sodium","title":"Sodium","categories":["fabric"]})")});
    fs::create_directories(home / "mods");
    {
        std::ofstream out(home / "mods" / "sodium-fabric-0.5.jar", std::ios::binary);
        const std::string jar = storedZip({{"fabric.mod.json", R"({"schemaVersion":1,"id":"sodium","version":"0.5","name":"Sodium"})"}});
        out.write(jar.data(), static_cast<std::streamsize>(jar.size()));
        std::ofstream modmenuFile(home / "mods" / "modmenu.jar", std::ios::binary);
        const std::string ok = storedZip({{"fabric.mod.json", R"({"schemaVersion":1,"id":"modmenu","version":"1"})"}});
        modmenuFile.write(ok.data(), static_cast<std::streamsize>(ok.size()));
    }
    Events events;
    AppOptions options;
    App app(appHttp, options, hooksFor(events), Blob{});
    (void)app.handleMessage(R"({"id":40,"method":"searchMods","params":{"query":""}})");
    const json found = answerOf(events, 40);
    CHECK(found["ok"].get<bool>() && found["result"]["hits"].size() == 3 && found["result"]["hits"][0]["blocked"].get<bool>() && found["result"]["hits"][1]["blocked"].get<bool>() && !found["result"]["hits"][2]["blocked"].get<bool>());
    CHECK(!found["result"]["hits"][0]["blockedReason"].get<std::string>().empty());
    (void)app.handleMessage(R"({"id":41,"method":"installMod","params":{"projectId":"AANobbMI","title":"Sodium","loader":""}})");
    const json refused = answerOf(events, 41);
    CHECK(!refused["ok"].get<bool>() && refused["error"]["code"] == "optimization_mod" && refused["error"]["message"].get<std::string>().find("Sodium") != std::string::npos);
    app.waitIdle();
    const json state = parse(app.stateJson());
    CHECK(state["mods"]["busy"].empty() && !state["mods"]["installed"].contains("AANobbMI"));
    CHECK(state["mods"]["blocked"].size() == 1 && state["mods"]["blocked"][0]["file"] == "sodium-fabric-0.5.jar" && !state["mods"]["blocked"][0]["reason"].get<std::string>().empty());

    // the demo mode refuses the same way (its first sample is filed under Optimization)
    _putenv_s("FALCON_LAUNCHER_HOME", freshFolder("modsblockdemo").string().c_str());
    _putenv_s("FALCON_LAUNCHER_MOCK_SPEED", "0.02");
    {
        FakeHttp demoHttp;
        Events demoEvents;
        AppOptions demoOptions;
        demoOptions.mockState = "home";
        App demo(demoHttp, demoOptions, hooksFor(demoEvents), Blob{});
        (void)demo.handleMessage(R"({"id":1,"method":"uiReady"})");
        (void)demo.handleMessage(R"({"id":50,"method":"searchMods","params":{"query":"lighting"}})");
        const json lighting = answerOf(demoEvents, 50);
        CHECK(lighting["result"]["hits"].size() == 1 && lighting["result"]["hits"][0]["blocked"].get<bool>());
        (void)demo.handleMessage(R"({"id":51,"method":"installMod","params":{"projectId":"mock0001","title":"Sample Lighting"}})");
        const json no = answerOf(demoEvents, 51);
        CHECK(!no["ok"].get<bool>() && no["error"]["code"] == "optimization_mod");
        demo.waitIdle();
    }
    _putenv_s("FALCON_LAUNCHER_MOCK_SPEED", "1");
}

int main() {
    _putenv_s("FALCON_LAUNCHER_NO_BROWSER", "1");
    std::printf("launcher tests\n");
    test_hash_and_encoding();
    test_safe_paths_and_urls();
    test_secure_store();
    test_browser_sign_in();
    test_refresh();
    test_avatar();
    test_minecraft_account();
    test_game_file_parsing();
    test_game_file_install();
    test_game_data_dir();
    test_bundled_client();
    test_signature_report();
    test_messages();
    test_mock_states();
    test_mock_flow();
    test_sign_in_outcomes();
    test_real_flow();
    test_not_configured_and_errors();
    test_mods_parts();
    test_mods_network();
    test_mods_messages();
    test_mods_blocker();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all launcher tests passed\n");
    return 0;
}
