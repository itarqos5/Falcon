#include "falcon/net/connection.hpp"

#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <stb_image.h>  // declarations only: zlib inflate for compressed packets

extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace falcon::net {

namespace {

SOCKET asSocket(std::uintptr_t s) { return static_cast<SOCKET>(s); }

void startWinsock() {
    static const bool once = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)once;
}

}  // namespace

bool Connection::connect(const std::string& host, int port, std::string& error) {
    startWinsock();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || found == nullptr) {
        error = "cannot find " + host;
        return false;
    }
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* a = found; a != nullptr; a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(found);
    if (s == INVALID_SOCKET) {
        error = "cannot connect to " + host + ":" + std::to_string(port);
        return false;
    }
    const int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
    socket_ = static_cast<std::uintptr_t>(s);
    open_.store(true);
    login_.store(true);
    threshold_.store(-1);
    reader_ = std::thread([this] { readLoop(); });
    return true;
}

bool Connection::readExact(std::uint8_t* out, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const int r = ::recv(asSocket(socket_), reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}

void Connection::setClosed(const std::string& why) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reason_.empty()) reason_ = why;
    open_.store(false);
}

std::string Connection::closedReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reason_;
}

void Connection::readLoop() {
    while (open_.load()) {
        // The length of the frame: a VarInt, a byte at a time.
        std::uint32_t length = 0;
        bool good = true;
        for (int shift = 0; shift < 35; shift += 7) {
            std::uint8_t b;
            if (!readExact(&b, 1)) { good = false; break; }
            length |= static_cast<std::uint32_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
        }
        if (!good || length == 0 || length > (1u << 24)) {
            setClosed(good ? "the server sent a bad packet length" : "the connection was closed");
            break;
        }
        Bytes frame(length);
        if (!readExact(frame.data(), frame.size())) {
            setClosed("the connection was closed");
            break;
        }
        Reader r(frame);
        Bytes plain;
        if (threshold_.load() >= 0) {
            const int dataLength = r.varint();
            if (dataLength > 0) {
                int outLength = 0;
                char* inflated = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(r.here()), static_cast<int>(r.left()), &outLength);
                if (inflated == nullptr) {
                    setClosed("a compressed packet could not be unpacked");
                    break;
                }
                plain.assign(reinterpret_cast<std::uint8_t*>(inflated), reinterpret_cast<std::uint8_t*>(inflated) + outLength);
                free(inflated);
            } else {
                plain.assign(r.here(), r.here() + r.left());
            }
        } else {
            plain.assign(r.here(), r.here() + r.left());
        }
        Reader p(plain);
        Packet packet;
        packet.id = p.varint();
        packet.body.assign(p.here(), p.here() + p.left());
        if (login_.load()) {
            if (packet.id == 3) {  // login compression: from the next packet on
                Reader t(packet.body);
                threshold_.store(t.varint());
            } else if (packet.id == 2) {  // login finished: the configuration phase starts (compression stays as it is)
                login_.store(false);
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(packet));
    }
    open_.store(false);
}

bool Connection::poll(Packet& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void Connection::send(int id, const Bytes& body) {
    if (!open_.load()) return;
    Writer inner;
    inner.varint(id);
    inner.raw(body);
    Writer frame;
    const int threshold = threshold_.load();
    if (threshold >= 0) {
        if (static_cast<int>(inner.data.size()) >= threshold) {
            int outLength = 0;
            unsigned char* z = stbi_zlib_compress(inner.data.data(), static_cast<int>(inner.data.size()), &outLength, 4);
            frame.varint(static_cast<std::int32_t>(inner.data.size()));
            if (z != nullptr) frame.data.insert(frame.data.end(), z, z + outLength);
            free(z);
        } else {
            frame.varint(0);
            frame.raw(inner.data);
        }
    } else {
        frame.raw(inner.data);
    }
    Writer out;
    out.varint(static_cast<std::int32_t>(frame.data.size()));
    out.raw(frame.data);
    std::lock_guard<std::mutex> lock(sendMutex_);
    std::size_t sent = 0;
    while (sent < out.data.size()) {
        const int r = ::send(asSocket(socket_), reinterpret_cast<const char*>(out.data.data() + sent), static_cast<int>(out.data.size() - sent), 0);
        if (r <= 0) {
            setClosed("the connection was closed");
            return;
        }
        sent += static_cast<std::size_t>(r);
    }
}

void Connection::close() {
    if (socket_ != ~static_cast<std::uintptr_t>(0)) {
        open_.store(false);
        shutdown(asSocket(socket_), SD_BOTH);
        closesocket(asSocket(socket_));
        socket_ = ~static_cast<std::uintptr_t>(0);
    }
    if (reader_.joinable()) reader_.join();
}

}  // namespace falcon::net
