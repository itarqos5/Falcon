#include "falcon/platform/ownership.hpp"

#include "falcon/platform/http.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace falcon {

namespace {

std::string normalizedId(std::string id) {
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    for (char& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return id;
}

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
    const HttpAnswer entitlements = httpsRequest("GET", "api.minecraftservices.com", "/entitlements/mcstore", accessToken);
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
    const HttpAnswer profile = httpsRequest("GET", "api.minecraftservices.com", "/minecraft/profile", accessToken);
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
