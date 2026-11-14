#pragma once

#include "http.hpp"
#include "result.hpp"

#include <string>

namespace launcher {

struct MicrosoftTokens {
    std::string accessToken;
    std::string refreshToken;
    int expiresInSeconds = 0;
};

// The Minecraft side of an account: whether it owns Java Edition, and the profile (name, id and skin) the game would use.
struct MinecraftAccount {
    std::string accessToken;  // for api.minecraftservices.com; short-lived
    int expiresInSeconds = 0;
    bool ownsGame = false;    // the entitlements list "game_minecraft" or "product_minecraft"
    bool hasProfile = false;
    std::string uuid;         // 32 hex digits, no dashes
    std::string name;
    std::string skinUrl;      // the active skin's texture on textures.minecraft.net, empty when there is none
};

// The Minecraft launcher's own public client id. Microsoft knows this id as "Minecraft" and lets any program that uses it sign a player in
// with the same page and the same redirect the official launcher uses (no Azure registration, no secret). It is the id every third-party
// launcher without an app of its own uses; see launcher/README.md for what that does and does not mean.
inline constexpr const char* kDefaultMinecraftClientId = "00000000402b5328";

// Where the sign-in page ends up when the player is done. Nothing is ever loaded from it: the launcher's sign-in window watches for the
// navigation, takes the code from the address and cancels the navigation.
inline constexpr const char* kRedirectUri = "https://login.live.com/oauth20_desktop.srf";

// What the redirect carried: a one-time code, or the reason there is none.
struct AuthRedirect {
    std::string code;
    std::string state;
    std::string error;             // "access_denied" when the player said no, ...
    std::string errorDescription;
};

// Signing in with a Microsoft account, the way the official launcher does it (no password ever touches Falcon):
//   1. the player signs in on Microsoft's own page, in a window of the launcher (authorizeUrl); the page ends at kRedirectUri with a code;
//   2. the code is traded for a Microsoft token (exchangeCode), that for an Xbox Live token, that for an XSTS token for the Minecraft
//      service, that for a Minecraft access token;
//   3. the Minecraft service is asked what the account owns (entitlements) and for its profile.
// The launcher keeps the refresh token (see secure_store.hpp), so step 1 happens once.
class AuthService {
public:
    AuthService(HttpClient& http, std::string clientId) : http_(http), clientId_(std::move(clientId)) {}

    [[nodiscard]] bool configured() const { return !clientId_.empty(); }

    // The address of Microsoft's sign-in page. `state` comes back unchanged with the redirect (checked by the caller).
    [[nodiscard]] std::string authorizeUrl(const std::string& state) const;
    // The code (or the error) in an address that begins with kRedirectUri; looks in the query and in the fragment. Empty when it is not one.
    [[nodiscard]] static AuthRedirect parseRedirect(const std::string& url);
    // The Microsoft tokens for the one-time code of the redirect.
    [[nodiscard]] Result<MicrosoftTokens> exchangeCode(const std::string& code);
    // A new access token from the saved refresh token (code "invalid_grant" when it has expired or been revoked: sign in again).
    [[nodiscard]] Result<MicrosoftTokens> refresh(const std::string& refreshToken);
    // Steps 2 and 3. Succeeds for an account that owns nothing too (ownsGame is false): the UI explains that, it is not a failure.
    [[nodiscard]] Result<MinecraftAccount> minecraftAccount(const std::string& microsoftAccessToken);

    // Exposed for tests: what the services say when something is wrong with the Xbox account (XSTS "XErr" codes).
    [[nodiscard]] static Error describeXstsError(long long xerr);

private:
    [[nodiscard]] Result<MicrosoftTokens> parseTokens(const HttpResponse& response);

    HttpClient& http_;
    std::string clientId_;
};

}  // namespace launcher
