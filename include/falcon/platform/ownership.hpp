#pragma once

#include <string>

namespace falcon {

// The proof that whoever runs the game owns Minecraft: Java Edition. The client that is bundled in the launcher asks Mojang's own servers, with the access token
// of the signed-in account that the launcher hands it (the FALCON_MC_TOKEN environment variable), whether that account owns the game and which profile it is:
// a token cannot be made up, so this cannot be passed by anyone who does not own the game. (It is a licence check, not copy protection: the source is open, and
// anyone who builds Falcon themselves can build it without the check. The Falcon.exe in dist/ is built that way, for development and tests.)
struct OwnershipCheck {
    bool ok = false;
    std::string message;  // when not ok: what to tell the player
};

// Asks api.minecraftservices.com (entitlements, then the profile) with `accessToken`; the profile must be the one with `uuid` and `name` (as far as they are
// given). Needs a network connection and takes a few hundred milliseconds.
[[nodiscard]] OwnershipCheck verifyMinecraftOwnership(const std::string& accessToken, const std::string& uuid, const std::string& name);

// The parts of that check that need no network (and are tested):
// whether the answer of /entitlements/mcstore lists the game ("game_minecraft" or "product_minecraft").
[[nodiscard]] bool entitlementsListGame(const std::string& body);
// whether the answer of /minecraft/profile is the profile of `uuid` (dashes and case do not matter) named `name`; `why` says what differs. An empty uuid or name
// is not compared.
[[nodiscard]] bool profileMatches(const std::string& body, const std::string& uuid, const std::string& name, std::string& why);

}  // namespace falcon
