#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

enum class ServerPackStatus { Enabled, Disabled, Prompt };  // the "Server Resource Packs" button's order

// One saved server (a compound in servers.dat).
struct ServerEntry {
    std::string name;
    std::string ip;                      // as typed: host, host:port, [v6]:port
    std::vector<unsigned char> iconPng;  // the server's 64x64 favicon as a PNG; empty = none
    ServerPackStatus packStatus = ServerPackStatus::Prompt;
    int acceptedCodeOfConduct = 0;       // kept so that saving does not lose it
    bool hidden = false;                 // a Direct Connection address, never shown in the list
};

// <game dir>/servers.dat: the saved servers. Entries are shared (not copied) so that whatever shows an
// entry keeps pointing at the same server when the list is reordered.
//
// The file is plain (uncompressed) NBT. Saving writes a temporary file and then swaps it in, keeping the
// previous file as servers.dat_old, so a crash while saving never leaves a half-written servers.dat.
class ServerList {
public:
    static constexpr std::size_t kMaxHidden = 16;

    explicit ServerList(std::filesystem::path gameDir) : gameDir_(std::move(gameDir)) {}

    // Reads servers.dat. A missing or unreadable file gives an empty list; entries read before a problem stay.
    void load();
    // Writes servers.dat; false if that failed (the old file is then still in place).
    bool save() const;

    [[nodiscard]] const std::vector<std::shared_ptr<ServerEntry>>& visible() const { return visible_; }
    [[nodiscard]] const std::vector<std::shared_ptr<ServerEntry>>& hiddenEntries() const { return hidden_; }

    // Adds at the end of the list shown to the player, or (hidden) at the front of the Direct Connection
    // history, which keeps only the newest kMaxHidden.
    void add(std::shared_ptr<ServerEntry> entry, bool hidden);
    // The first entry whose address is exactly `ip`, shown ones first; null if there is none.
    [[nodiscard]] std::shared_ptr<ServerEntry> find(const std::string& ip) const;
    // Moves a hidden entry with this address to the end of the shown list and returns it (null if none).
    std::shared_ptr<ServerEntry> unhide(const std::string& ip);
    void remove(const std::shared_ptr<ServerEntry>& entry);
    // Swaps two shown entries and saves, so the new order survives.
    void swap(std::size_t a, std::size_t b);

    [[nodiscard]] std::filesystem::path path() const { return gameDir_ / "servers.dat"; }

private:
    std::filesystem::path gameDir_;
    std::vector<std::shared_ptr<ServerEntry>> visible_;
    std::vector<std::shared_ptr<ServerEntry>> hidden_;
};

}  // namespace falcon
