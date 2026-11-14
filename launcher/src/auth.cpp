#include "auth.hpp"

#include "util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace launcher {

using json = nlohmann::json;

namespace {

// Microsoft's account service as the Minecraft launcher's client id uses it ("Live Connect": the older of Microsoft's two OAuth endpoints, the
// one the official launcher's id is registered with). The scope asks for a ticket the Xbox Live service accepts.
constexpr const char* kAuthorizeUrl = "https://login.live.com/oauth20_authorize.srf";
constexpr const char* kTokenUrl = "https://login.live.com/oauth20_token.srf";
constexpr const char* kScope = "service::user.auth.xboxlive.com::MBI_SSL";

constexpr const char* kXblUrl = "https://user.auth.xboxlive.com/user/authenticate";
constexpr const char* kXstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr const char* kMinecraftLoginUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr const char* kEntitlementsUrl = "https://api.minecraftservices.com/entitlements/mcstore";
constexpr const char* kProfileUrl = "https://api.minecraftservices.com/minecraft/profile";

const HttpHeaders kFormHeaders = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
const HttpHeaders kJsonHeaders = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};

json parseJson(const std::string& text) { return json::parse(text, nullptr, /*allow_exceptions=*/false); }

std::string stringOf(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int intOf(const json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int>() : fallback;
}

Error networkError(const HttpResponse& r, const char* what) {
    if (r.status == 0) return Error{"network", std::string("Could not reach ") + what + ": " + r.error};
    return Error{"service", std::string(what) + " answered with an error (" + std::to_string(r.status) + ")"};
}

}  // namespace

std::string AuthService::authorizeUrl(const std::string& state) const {
    return std::string(kAuthorizeUrl) + "?" + formEncode({{"client_id", clientId_}, {"response_type", "code"}, {"redirect_uri", kRedirectUri}, {"scope", kScope}, {"state", state}});
}

AuthRedirect AuthService::parseRedirect(const std::string& url) {
    AuthRedirect out;
    const std::string base = kRedirectUri;
    if (url.size() < base.size() || url.compare(0, base.size(), base) != 0) return out;
    if (url.size() > base.size() && url[base.size()] != '?' && url[base.size()] != '#') return out;  // (oauth20_desktop.srfx is not it)
    const std::size_t start = base.size() < url.size() ? base.size() + 1 : url.size();
    std::size_t pos = start;
    while (pos < url.size()) {
        std::size_t end = url.find_first_of("&#", pos);
        if (end == std::string::npos) end = url.size();
        const std::string pair = url.substr(pos, end - pos);
        const std::size_t eq = pair.find('=');
        const std::string key = urlDecode(pair.substr(0, eq));
        const std::string value = eq == std::string::npos ? std::string() : urlDecode(pair.substr(eq + 1));
        if (key == "code") out.code = value;
        else if (key == "state") out.state = value;
        else if (key == "error") out.error = value;
        else if (key == "error_description") out.errorDescription = value;
        pos = end + 1;
    }
    return out;
}

Result<MicrosoftTokens> AuthService::parseTokens(const HttpResponse& r) {
    const json j = parseJson(r.body);
    if (j.is_discarded() || !j.is_object()) return Result<MicrosoftTokens>::fail("bad_response", "Microsoft's answer was not understood.");
    MicrosoftTokens t;
    t.accessToken = stringOf(j, "access_token");
    t.refreshToken = stringOf(j, "refresh_token");
    t.expiresInSeconds = intOf(j, "expires_in", 3600);
    if (t.accessToken.empty()) return Result<MicrosoftTokens>::fail("bad_response", "Microsoft's answer had no access token.");
    return Result<MicrosoftTokens>::success(std::move(t));
}

Result<MicrosoftTokens> AuthService::exchangeCode(const std::string& code) {
    if (!configured()) return Result<MicrosoftTokens>::fail("not_configured", "This build of the launcher has no Microsoft application id, so it cannot sign anyone in. See launcher/README.md.");
    const HttpResponse r = http_.post(kTokenUrl, kFormHeaders,
                                      formEncode({{"grant_type", "authorization_code"}, {"client_id", clientId_}, {"code", code}, {"redirect_uri", kRedirectUri}, {"scope", kScope}}));
    if (r.status == 0) return Result<MicrosoftTokens>::fail(networkError(r, "Microsoft"));
    if (!r.ok()) {
        const json j = parseJson(r.body);
        const std::string error = j.is_object() ? stringOf(j, "error") : std::string();
        const std::string detail = j.is_object() ? stringOf(j, "error_description") : std::string();
        return Result<MicrosoftTokens>::fail("service", "Microsoft did not accept the sign-in" + (error.empty() ? std::string(".") : " (" + error + ")") + (detail.empty() ? "" : ": " + detail.substr(0, 200)));
    }
    return parseTokens(r);
}

Result<MicrosoftTokens> AuthService::refresh(const std::string& refreshToken) {
    if (!configured()) return Result<MicrosoftTokens>::fail("not_configured", "No Microsoft application id.");
    const HttpResponse r = http_.post(kTokenUrl, kFormHeaders,
                                      formEncode({{"grant_type", "refresh_token"}, {"client_id", clientId_}, {"refresh_token", refreshToken}, {"redirect_uri", kRedirectUri}, {"scope", kScope}}));
    if (r.status == 0) return Result<MicrosoftTokens>::fail(networkError(r, "Microsoft"));
    if (!r.ok()) {
        const json j = parseJson(r.body);
        const std::string error = j.is_object() ? stringOf(j, "error") : std::string();
        if (error == "invalid_grant" || error == "interaction_required") return Result<MicrosoftTokens>::fail("invalid_grant", "Your sign-in has expired. Please sign in again.");
        return Result<MicrosoftTokens>::fail("service", "Microsoft refused to renew the sign-in (" + std::to_string(r.status) + ").");
    }
    auto tokens = parseTokens(r);
    if (tokens.ok() && tokens->refreshToken.empty()) tokens->refreshToken = refreshToken;  // (kept if Microsoft does not rotate it)
    return tokens;
}

Error AuthService::describeXstsError(long long xerr) {
    switch (xerr) {
        case 2148916233LL: return {"xbox_no_account", "This Microsoft account has no Xbox profile yet. Sign in once at xbox.com to create one, then try again."};
        case 2148916235LL: return {"xbox_region", "Xbox Live is not available in this account's country or region."};
        case 2148916236LL:
        case 2148916237LL: return {"xbox_adult", "This account needs adult verification on the Xbox site before it can be used."};
        case 2148916238LL: return {"xbox_child", "This is a child account. An adult has to add it to a Microsoft family before it can play online."};
        default: return {"xbox_denied", "Xbox Live refused this account (error " + std::to_string(xerr) + ")."};
    }
}

Result<MinecraftAccount> AuthService::minecraftAccount(const std::string& microsoftAccessToken) {
    using R = Result<MinecraftAccount>;
    // Xbox Live user token. The ticket is sent with the prefix Xbox Live expects for it; which one that is depends on the kind of Microsoft
    // token, so the usual two are tried in turn (a wrong one is refused at once, with a 4xx, before anything else happens).
    const bool prefixed = microsoftAccessToken.rfind("d=", 0) == 0 || microsoftAccessToken.rfind("t=", 0) == 0;
    const std::vector<std::string> tickets = prefixed ? std::vector<std::string>{microsoftAccessToken}
                                                      : std::vector<std::string>{"d=" + microsoftAccessToken, "t=" + microsoftAccessToken};
    HttpResponse xbl;
    for (const std::string& ticket : tickets) {
        const json xblBody = {{"Properties", {{"AuthMethod", "RPS"}, {"SiteName", "user.auth.xboxlive.com"}, {"RpsTicket", ticket}}},
                              {"RelyingParty", "http://auth.xboxlive.com"},
                              {"TokenType", "JWT"}};
        xbl = http_.post(kXblUrl, kJsonHeaders, xblBody.dump());
        if (xbl.ok() || xbl.status == 0 || xbl.status >= 500) break;
    }
    if (!xbl.ok()) return R::fail(networkError(xbl, "Xbox Live"));
    const json xblJson = parseJson(xbl.body);
    const std::string xblToken = xblJson.is_object() ? stringOf(xblJson, "Token") : std::string();
    std::string userHash;
    if (xblJson.is_object() && xblJson.contains("DisplayClaims") && xblJson["DisplayClaims"].contains("xui") && xblJson["DisplayClaims"]["xui"].is_array() &&
        !xblJson["DisplayClaims"]["xui"].empty()) {
        userHash = stringOf(xblJson["DisplayClaims"]["xui"][0], "uhs");
    }
    if (xblToken.empty() || userHash.empty()) return R::fail("bad_response", "Xbox Live's answer was not understood.");

    // XSTS token for the Minecraft service.
    const json xstsBody = {{"Properties", {{"SandboxId", "RETAIL"}, {"UserTokens", json::array({xblToken})}}},
                           {"RelyingParty", "rp://api.minecraftservices.com/"},
                           {"TokenType", "JWT"}};
    const HttpResponse xsts = http_.post(kXstsUrl, kJsonHeaders, xstsBody.dump());
    if (!xsts.ok()) {
        const json j = parseJson(xsts.body);
        if (xsts.status == 401 && j.is_object() && j.contains("XErr") && j["XErr"].is_number_integer()) return R::fail(describeXstsError(j["XErr"].get<long long>()));
        return R::fail(networkError(xsts, "Xbox Live"));
    }
    const json xstsJson = parseJson(xsts.body);
    const std::string xstsToken = xstsJson.is_object() ? stringOf(xstsJson, "Token") : std::string();
    if (xstsToken.empty()) return R::fail("bad_response", "Xbox Live's answer was not understood.");

    // Minecraft access token.
    const json mcBody = {{"identityToken", "XBL3.0 x=" + userHash + ";" + xstsToken}};
    const HttpResponse mc = http_.post(kMinecraftLoginUrl, kJsonHeaders, mcBody.dump());
    if (!mc.ok()) {
        const json j = parseJson(mc.body);
        const std::string detail = j.is_object() ? stringOf(j, "errorMessage") + stringOf(j, "error") : std::string();
        if (mc.status == 403 && detail.find("app registration") != std::string::npos) {
            return R::fail("app_not_approved", "Mojang has not approved this launcher's Microsoft application for Minecraft yet (https://aka.ms/AppRegInfo).");
        }
        return R::fail(networkError(mc, "the Minecraft service"));
    }
    const json mcJson = parseJson(mc.body);
    MinecraftAccount account;
    account.accessToken = mcJson.is_object() ? stringOf(mcJson, "access_token") : std::string();
    account.expiresInSeconds = mcJson.is_object() ? intOf(mcJson, "expires_in", 86400) : 86400;
    if (account.accessToken.empty()) return R::fail("bad_response", "The Minecraft service's answer was not understood.");
    const HttpHeaders bearer = {{"Authorization", "Bearer " + account.accessToken}, {"Accept", "application/json"}};

    // What the account owns.
    const HttpResponse ent = http_.get(kEntitlementsUrl, bearer);
    if (!ent.ok()) return R::fail(networkError(ent, "the Minecraft service"));
    const json entJson = parseJson(ent.body);
    if (entJson.is_object() && entJson.contains("items") && entJson["items"].is_array()) {
        for (const json& item : entJson["items"]) {
            const std::string name = item.is_object() ? stringOf(item, "name") : std::string();
            if (name == "game_minecraft" || name == "product_minecraft") account.ownsGame = true;
        }
    }

    // The profile (a name, an id and the skins). Accounts that own the game but never chose a name have none (404).
    const HttpResponse profile = http_.get(kProfileUrl, bearer);
    if (profile.ok()) {
        const json pj = parseJson(profile.body);
        if (pj.is_object()) {
            account.uuid = stringOf(pj, "id");
            account.name = stringOf(pj, "name");
            account.hasProfile = !account.uuid.empty() && !account.name.empty();
            if (pj.contains("skins") && pj["skins"].is_array()) {
                for (const json& skin : pj["skins"]) {
                    if (!skin.is_object()) continue;
                    const std::string url = stringOf(skin, "url");
                    if (url.empty()) continue;
                    if (account.skinUrl.empty() || stringOf(skin, "state") == "ACTIVE") account.skinUrl = url;
                }
            }
        }
    } else if (profile.status != 404) {
        return R::fail(networkError(profile, "the Minecraft service"));
    }
    return R::success(std::move(account));
}

}  // namespace launcher
