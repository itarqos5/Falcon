#pragma once

#include "falcon/net/bytes.hpp"
#include "falcon/net/crypto.hpp"

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
// compression (0: not compressed), then the id and the body. A thread reads and unpacks; the main thread takes packets off the queue. A server in online mode asks for encryption once the login has begun: from then on every byte is AES-128/CFB8 (sendAndEncrypt), under the compression.
class Connection {
public:
    Connection() = default;
    ~Connection() { close(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Blocks until connected or failed (a few seconds at most).
    bool connect(const std::string& host, int port, std::string& error);
    void send(int id, const Bytes& body);
    // Sends this packet as it is (the answer to the server's request for encryption, which carries the shared secret), and from then on encrypts everything that is sent and
    // decrypts everything that is received, with `secret` (16 bytes) as the key and the first IV. False when the system's cipher cannot be set up (nothing is sent then).
    bool sendAndEncrypt(int id, const Bytes& body, const std::uint8_t* secret);
    [[nodiscard]] bool encrypted() const { return decrypting_.load(); }
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
    [[nodiscard]] static Bytes frameOf(int id, const Bytes& body, int threshold);
    void transmit(Bytes& bytes);  // (under the send lock) encrypts when that is on, then writes it all

    std::uintptr_t socket_ = ~static_cast<std::uintptr_t>(0);
    std::thread reader_;
    std::atomic<bool> open_{false};
    std::atomic<bool> login_{true};
    std::atomic<int> threshold_{-1};
    mutable std::mutex mutex_;
    std::deque<Packet> queue_;
    std::string reason_;
    std::mutex sendMutex_;
    Cfb8 sendCipher_;                      // used under sendMutex_
    bool encrypting_ = false;              // (under sendMutex_)
    Cfb8 receiveCipher_;                   // used by the reader thread alone
    std::atomic<bool> decrypting_{false};  // set before the packet that starts the encryption is sent
};

}  // namespace falcon::net
