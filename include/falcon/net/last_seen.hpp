#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace falcon::net {

using MessageSignature = std::array<std::uint8_t, 256>;

// Which signed chat messages of others this client has seen (the game's LastSeenMessagesTracker): the server sends each of them signed, and every message the client says
// afterwards must tell which of the last twenty it had seen (a bit each, oldest first) so that the signature can cover them; a count of those that came since goes with it,
// and after sixty-four unreported ones the client says so on its own (a chat acknowledgement).
class LastSeenTracker {
public:
    static constexpr int kSize = 20;
    static constexpr int kReportAfter = 64;

    // A signed message arrived (and was shown). False when it is the one that came just before (the server sends a message again to those it was meant for twice).
    bool addPending(const MessageSignature& signature);
    // How many messages came since the last time this was asked, which resets it (what a chat acknowledgement carries).
    int takeOffset();
    [[nodiscard]] int offset() const { return offset_; }

    // What a message says about them (LastSeenMessages.Update, and the signatures the message's signature covers), and every one of them counts as acknowledged from then on.
    struct Update {
        int offset = 0;
        std::uint32_t acknowledged = 0;  // bit j: the entry j places after the oldest is there
        std::uint8_t checksum = 1;       // (LastSeenMessages.computeChecksum: 1 for none)
        std::vector<MessageSignature> seen;
    };
    [[nodiscard]] Update generateAndApplyUpdate();

private:
    struct Entry {
        MessageSignature signature;
        bool pending = true;
    };
    std::array<std::optional<Entry>, kSize> ring_{};
    int tail_ = 0;
    int offset_ = 0;
    std::optional<MessageSignature> last_;
};

// Arrays.hashCode of a signature's bytes (as Java's signed bytes): what MessageSignature.checksum gives.
[[nodiscard]] std::int32_t signatureChecksum(const MessageSignature& signature);

}  // namespace falcon::net
