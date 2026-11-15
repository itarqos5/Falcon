#pragma once

#include "falcon/net/bytes.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace falcon::net {

// What a server in online mode wants from the player, asked of Mojang's servers with the access token of the signed-in account (the launcher hands it over).

// The key pair that signs chat (the profile key pair the game keeps), with Mojang's signature on the public half.
struct ChatKeys {
    Bytes privateKey;   // DER, PKCS#8
    Bytes publicKey;    // DER, X.509 SubjectPublicKeyInfo
    Bytes signature;    // Mojang's signature of the public key (the version that includes the player's id)
    std::int64_t expiresAtMillis = 0;
};

// Tells the session server that this account is joining the server whose login hash this is (POST /session/minecraft/join); the server then asks the session server whether the
// player really did. `profileId` is the account's id (with or without dashes). False and `error` (in words for the player) when it is refused or cannot be reached.
[[nodiscard]] bool joinSession(const std::string& accessToken, const std::string& profileId, const std::string& serverHash, std::string& error);

// The id (without dashes) and the name of the account's Java Edition profile (GET /minecraft/profile).
[[nodiscard]] bool fetchProfile(const std::string& accessToken, std::string& id, std::string& name, std::string& error);

// The chat signing key pair (POST /player/certificates). Like the game, it keeps the keys on disk (%LOCALAPPDATA%\Falcon\profilekeys) and asks Mojang only when they are
// nearly spent or not there, because that request is limited hard; keys that were kept still serve when Mojang refuses or cannot be reached.
[[nodiscard]] bool fetchChatKeys(const std::string& accessToken, const std::string& profileId, ChatKeys& out, std::string& error);

// The parts that need no network (and are tested):
[[nodiscard]] Bytes base64Decode(const std::string& text);                  // whitespace is skipped; empty for text that is not base64
[[nodiscard]] Bytes pemBody(const std::string& pem);                        // the data between "-----BEGIN ...-----" and "-----END ...-----", decoded
[[nodiscard]] std::int64_t parseIsoMillis(const std::string& text);          // "2026-11-15T10:15:30.123456Z" as milliseconds since 1970; -1 for text that is not that
[[nodiscard]] bool parseChatKeysResponse(const std::string& body, ChatKeys& out, std::string& error);
[[nodiscard]] bool parseUuid(const std::string& text, std::array<std::uint8_t, 16>& out);  // 32 hex digits, dashes allowed anywhere
[[nodiscard]] std::string uuidWithoutDashes(const std::array<std::uint8_t, 16>& id);

// What the signature of a chat message covers (PlayerChatMessage.updateSignature, SignedMessageLink and SignedMessageBody): the version 1, who sends, in which chat session,
// the number of the message in the chain, the salt, the time in whole seconds, the message (its length in bytes, then the bytes) and the signatures of the messages of others
// that were seen (their count, then each one). Whole numbers are big-endian.
[[nodiscard]] Bytes chatSignaturePayload(const std::array<std::uint8_t, 16>& sender, const std::array<std::uint8_t, 16>& session, int index, std::int64_t salt, std::int64_t seconds,
                                         const std::string& text, const std::vector<std::array<std::uint8_t, 256>>& seen);

}  // namespace falcon::net
