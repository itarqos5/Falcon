#include "falcon/net/last_seen.hpp"

namespace falcon::net {

std::int32_t signatureChecksum(const MessageSignature& signature) {
    std::uint32_t hash = 1;
    for (const std::uint8_t b : signature) hash = hash * 31u + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(b)));
    return static_cast<std::int32_t>(hash);
}

bool LastSeenTracker::addPending(const MessageSignature& signature) {
    if (last_ && *last_ == signature) return false;
    last_ = signature;
    const int slot = tail_;
    tail_ = (tail_ + 1) % kSize;
    ++offset_;
    ring_[static_cast<std::size_t>(slot)] = Entry{signature, true};
    return true;
}

int LastSeenTracker::takeOffset() {
    const int offset = offset_;
    offset_ = 0;
    return offset;
}

LastSeenTracker::Update LastSeenTracker::generateAndApplyUpdate() {
    Update update;
    update.offset = takeOffset();
    std::int32_t checksum = 1;
    for (int j = 0; j < kSize; ++j) {
        std::optional<Entry>& entry = ring_[static_cast<std::size_t>((tail_ + j) % kSize)];
        if (!entry) continue;
        update.acknowledged |= 1u << j;
        update.seen.push_back(entry->signature);
        checksum = static_cast<std::int32_t>(static_cast<std::uint32_t>(checksum) * 31u + static_cast<std::uint32_t>(signatureChecksum(entry->signature)));
        entry->pending = false;  // (it stays in the window, acknowledged)
    }
    const std::int8_t low = static_cast<std::int8_t>(checksum);
    update.checksum = static_cast<std::uint8_t>(low == 0 ? 1 : low);
    return update;
}

}  // namespace falcon::net
