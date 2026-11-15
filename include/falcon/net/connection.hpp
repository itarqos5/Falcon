#pragma once

#include "falcon/net/bytes.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace falcon::net {

struct Packet {
    int id = 0;
    Bytes body;
};

// One TCP connection to a server, speaking the game's packet framing: a VarInt length, then (once the server asks for compression) a VarInt of the size before
// compression (0: not compressed), then the id and the body. A thread reads and unpacks; the main thread takes packets off the queue. Encryption is not done here (a
// server in online mode is refused with a message), so this talks to servers that run with online-mode=false.
class Connection {
public:
    Connection() = default;
    ~Connection() { close(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Blocks until connected or failed (a few seconds at most).
    bool connect(const std::string& host, int port, std::string& error);
    void send(int id, const Bytes& body);
    // The oldest packet that arrived, if any.
    bool poll(Packet& out);
    void close();
    [[nodiscard]] bool open() const { return open_.load(); }
    [[nodiscard]] std::string closedReason() const;
    // While logging in, the reader itself switches compression on when the server asks (login packet 3), so no packet is unpacked the wrong way.
    void setLoginPhase(bool on) { login_.store(on); }
    [[nodiscard]] int compressionThreshold() const { return threshold_.load(); }

private:
    void readLoop();
    bool readExact(std::uint8_t* out, std::size_t n);
    void setClosed(const std::string& why);

    std::uintptr_t socket_ = ~static_cast<std::uintptr_t>(0);
    std::thread reader_;
    std::atomic<bool> open_{false};
    std::atomic<bool> login_{true};
    std::atomic<int> threshold_{-1};
    mutable std::mutex mutex_;
    std::deque<Packet> queue_;
    std::string reason_;
    std::mutex sendMutex_;
};

}  // namespace falcon::net
