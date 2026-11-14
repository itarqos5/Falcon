#pragma once

#include "apex/text/styled_text.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace apex {

// The protocol number of the game version this client reports to servers (26.3).
inline constexpr int kProtocolVersion = 777;

enum class PingState { Pinging, Unreachable, Incompatible, Successful };

// What the message-of-the-day line says when the server has not supplied one (the screen translates it).
enum class PingMessage { None, Pinging, CannotConnect, CannotResolve };

// Everything a ping has found out so far. Copied out of a running ping with ServerPing::snapshot().
struct PingSnapshot {
    PingState state = PingState::Pinging;
    PingMessage message = PingMessage::Pinging;  // shown instead of `motd` unless None
    StyledText motd;
    bool hasPlayers = false;
    int playersOnline = 0;
    int playersMax = 0;
    std::vector<std::string> playerSample;  // names; an empty string is the "Anonymous Player"
    int playersNotListed = 0;               // online players beyond the sample
    std::string versionName;                // empty with versionIsOld: the server sent no version
    bool versionIsOld = false;
    int protocol = 0;
    std::int64_t pingMillis = 0;
    std::vector<unsigned char> faviconPng;  // empty: the server sent none
    std::uint64_t revision = 0;             // grows every time anything above changes
};

// One status ping ("Server List Ping") of one server address, running on a background thread: it
// resolves the name (following a _minecraft._tcp SRV record when the port is the default one),
// connects, asks for the server's status and times a ping/pong. It opens a single TCP connection and
// sends nothing but the status handshake: it never logs in.
//
// A ping never blocks the caller. At most five run at once; further ones wait their turn.
class ServerPing {
public:
    // Starts pinging `address` (as the player typed it: host, host:port, [v6]:port).
    static std::shared_ptr<ServerPing> start(const std::string& address);
    ~ServerPing();

    [[nodiscard]] PingSnapshot snapshot() const;
    // Stops the ping as soon as possible (a blocked connect or read is interrupted). Its results are dropped.
    void cancel();

    // For tests: pings `host:port` right here on the calling thread and returns the outcome.
    static PingSnapshot runBlocking(const std::string& host, int port);

private:
    ServerPing() = default;
    void run(const std::string& address);
    void update(const std::function<void(PingSnapshot&)>& change);

    mutable std::mutex mutex_;
    PingSnapshot snapshot_;
    std::atomic<bool> cancelled_{false};
    std::atomic<std::uintptr_t> socket_{static_cast<std::uintptr_t>(-1)};  // the open connection, to interrupt it

    friend struct PingRun;
};

}  // namespace apex
