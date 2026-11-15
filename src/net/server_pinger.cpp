#include "falcon/net/server_pinger.hpp"

#include "falcon/util/base64.hpp"
#include "falcon/util/server_address.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <semaphore>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <poll.h>
#include <resolv.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace falcon {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kBadSocket = INVALID_SOCKET;
void closeSocket(SocketHandle s) { closesocket(s); }
int lastSocketError() { return WSAGetLastError(); }
bool wouldBlock(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
#else
using SocketHandle = int;
constexpr SocketHandle kBadSocket = -1;
void closeSocket(SocketHandle s) { close(s); }
int lastSocketError() { return errno; }
bool wouldBlock(int error) { return error == EINPROGRESS || error == EWOULDBLOCK; }
#endif

constexpr int kConnectTimeoutMs = 10000;
constexpr int kReadTimeoutMs = 30000;  // the game's ReadTimeoutHandler(30)
constexpr std::size_t kMaxStatusBytes = 32767 * 3 + 3;
constexpr std::uintptr_t kNoSocket = static_cast<std::uintptr_t>(-1);

void initNetworking() {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

std::int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- wire format -------------------------------------------------------------------------

void writeVarInt(std::vector<unsigned char>& out, std::uint32_t value) {
    do {
        unsigned char b = value & 0x7F;
        value >>= 7;
        if (value != 0) b |= 0x80;
        out.push_back(b);
    } while (value != 0);
}

void writeString(std::vector<unsigned char>& out, const std::string& s) {
    writeVarInt(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

// A packet on the wire: its length as a VarInt, then its id and fields.
std::vector<unsigned char> framed(const std::vector<unsigned char>& body) {
    std::vector<unsigned char> out;
    writeVarInt(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool sendAll(SocketHandle s, const std::vector<unsigned char>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(s, reinterpret_cast<const char*>(data.data()) + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recvExact(SocketHandle s, unsigned char* out, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const int r = recv(s, reinterpret_cast<char*>(out) + got, static_cast<int>(n - got), 0);
        if (r <= 0) return false;  // closed, failed or timed out
        got += static_cast<std::size_t>(r);
    }
    return true;
}

bool readVarInt(SocketHandle s, std::int32_t& value) {
    std::uint32_t result = 0;
    for (int i = 0; i < 5; ++i) {
        unsigned char b = 0;
        if (!recvExact(s, &b, 1)) return false;
        result |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
        if ((b & 0x80) == 0) {
            value = static_cast<std::int32_t>(result);
            return true;
        }
    }
    return false;  // longer than five bytes
}

struct Frame {
    std::int32_t id = 0;
    std::vector<unsigned char> payload;
};

std::optional<Frame> readFrame(SocketHandle s) {
    std::int32_t length = 0;
    if (!readVarInt(s, length) || length < 1 || static_cast<std::size_t>(length) > kMaxStatusBytes + 16) return std::nullopt;
    std::vector<unsigned char> body(static_cast<std::size_t>(length));
    if (!recvExact(s, body.data(), body.size())) return std::nullopt;
    // The packet id is a VarInt at the start of the body.
    Frame frame;
    std::size_t pos = 0;
    std::uint32_t id = 0;
    for (int i = 0; i < 5 && pos < body.size(); ++i) {
        const unsigned char b = body[pos++];
        id |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
        if ((b & 0x80) == 0) {
            frame.id = static_cast<std::int32_t>(id);
            frame.payload.assign(body.begin() + static_cast<std::ptrdiff_t>(pos), body.end());
            return frame;
        }
    }
    return std::nullopt;
}

// A status response's only field: a VarInt-prefixed UTF-8 string of JSON.
std::optional<std::string> readJsonString(const std::vector<unsigned char>& payload) {
    std::uint32_t length = 0;
    std::size_t pos = 0;
    for (int i = 0; i < 5; ++i) {
        if (pos >= payload.size()) return std::nullopt;
        const unsigned char b = payload[pos++];
        length |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
        if ((b & 0x80) == 0) break;
        if (i == 4) return std::nullopt;
    }
    if (length > kMaxStatusBytes || payload.size() - pos < length) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(payload.data()) + pos, length);
}

// ---- the status JSON -------------------------------------------------------------------------

constexpr const char kFaviconPrefix[] = "data:image/png;base64,";

// Reads what a status response says; every field is optional and a malformed one is ignored.
void applyStatus(const nlohmann::json& status, PingSnapshot& out) {
    out.message = PingMessage::None;
    out.hasPlayers = false;
    out.playerSample.clear();
    out.playersNotListed = 0;
    out.motd.clear();
    out.faviconPng.clear();

    if (const auto d = status.find("description"); d != status.end()) out.motd = parseChatComponent(*d);

    const auto version = status.find("version");
    if (version != status.end() && version->is_object()) {
        const auto name = version->find("name");
        const auto protocol = version->find("protocol");
        out.versionName = name != version->end() && name->is_string() ? name->get<std::string>() : std::string();
        out.protocol = protocol != version->end() && protocol->is_number_integer() ? protocol->get<int>() : 0;
        out.versionIsOld = false;
    } else {
        out.versionName.clear();
        out.versionIsOld = true;  // the server sent no version: the game calls it "Old", protocol 0
        out.protocol = 0;
    }

    const auto players = status.find("players");
    if (players != status.end() && players->is_object()) {
        const auto max = players->find("max");
        const auto online = players->find("online");
        if (max != players->end() && max->is_number_integer() && online != players->end() && online->is_number_integer()) {
            out.hasPlayers = true;
            out.playersMax = max->get<int>();
            out.playersOnline = online->get<int>();
            if (const auto sample = players->find("sample"); sample != players->end() && sample->is_array()) {
                for (const auto& p : *sample) {
                    if (!p.is_object()) continue;
                    const auto id = p.find("id");
                    const auto name = p.find("name");
                    if (id == p.end() || !id->is_string() || name == p.end() || !name->is_string()) continue;
                    const bool anonymous = id->get<std::string>() == "00000000-0000-0000-0000-000000000000" &&
                                           name->get<std::string>() == "Anonymous Player";
                    out.playerSample.push_back(anonymous ? std::string() : name->get<std::string>());
                }
                if (static_cast<int>(out.playerSample.size()) < out.playersOnline) {
                    out.playersNotListed = out.playersOnline - static_cast<int>(out.playerSample.size());
                }
            }
        }
    }

    if (const auto favicon = status.find("favicon"); favicon != status.end() && favicon->is_string()) {
        const std::string& text = favicon->get_ref<const std::string&>();
        if (text.rfind(kFaviconPrefix, 0) == 0) {
            if (auto bytes = base64Decode(std::string_view(text).substr(sizeof(kFaviconPrefix) - 1))) out.faviconPng = std::move(*bytes);
        }
    }
}

// ---- name resolution and connecting ---------------------------------------------------------

bool isIpLiteral(const std::string& host) {
    unsigned char buffer[sizeof(struct in6_addr)];
    return inet_pton(AF_INET, host.c_str(), buffer) == 1 || inet_pton(AF_INET6, host.c_str(), buffer) == 1;
}

// _minecraft._tcp.<host> SRV record: where the server really is, when its address has the default
// port. Only Windows has a resolver call for it here; elsewhere the address is used as it is.
std::optional<ServerAddress> lookupSrv(const std::string& host) {
#ifdef _WIN32
    const std::string name = "_minecraft._tcp." + host;
    DNS_RECORDA* records = nullptr;
    if (DnsQuery_A(name.c_str(), DNS_TYPE_SRV, DNS_QUERY_STANDARD, nullptr, reinterpret_cast<PDNS_RECORD*>(&records), nullptr) != 0 ||
        records == nullptr) {
        return std::nullopt;
    }
    std::optional<ServerAddress> best;
    int bestPriority = 0x10000;
    int bestWeight = -1;
    for (DNS_RECORDA* r = records; r != nullptr; r = r->pNext) {
        if (r->wType != DNS_TYPE_SRV || r->Data.SRV.pNameTarget == nullptr) continue;
        const int priority = r->Data.SRV.wPriority;
        const int weight = r->Data.SRV.wWeight;
        if (priority < bestPriority || (priority == bestPriority && weight > bestWeight)) {
            bestPriority = priority;
            bestWeight = weight;
            std::string target = r->Data.SRV.pNameTarget;
            while (!target.empty() && target.back() == '.') target.pop_back();
            best = ServerAddress{target, r->Data.SRV.wPort};
        }
    }
    DnsRecordListFree(records, DnsFreeRecordList);
    return best;
#else
    // The system's resolver (res_query, in libc since glibc 2.34 and in libresolv before): the answer is a DNS message to read the SRV records out of.
    const std::string name = "_minecraft._tcp." + host;
    unsigned char answer[4096];
    const int length = res_query(name.c_str(), ns_c_in, ns_t_srv, answer, sizeof(answer));
    if (length <= 0) return std::nullopt;
    ns_msg message;
    if (ns_initparse(answer, length, &message) != 0) return std::nullopt;
    std::optional<ServerAddress> best;
    int bestPriority = 0x10000;
    int bestWeight = -1;
    for (int i = 0; i < ns_msg_count(message, ns_s_an); ++i) {
        ns_rr record;
        if (ns_parserr(&message, ns_s_an, i, &record) != 0 || ns_rr_type(record) != ns_t_srv || ns_rr_rdlen(record) < 7) continue;
        const unsigned char* data = ns_rr_rdata(record);  // priority, weight, port (each 16 bits, big-endian), then the target's name
        const int priority = (data[0] << 8) | data[1];
        const int weight = (data[2] << 8) | data[3];
        const int port = (data[4] << 8) | data[5];
        char target[NS_MAXDNAME] = {};
        if (dn_expand(ns_msg_base(message), ns_msg_end(message), data + 6, target, sizeof(target)) < 0) continue;
        if (priority < bestPriority || (priority == bestPriority && weight > bestWeight)) {
            bestPriority = priority;
            bestWeight = weight;
            std::string targetName = target;
            while (!targetName.empty() && targetName.back() == '.') targetName.pop_back();
            best = ServerAddress{targetName, port};
        }
    }
    return best;
#endif
}

void setTimeouts(SocketHandle s, int milliseconds) {
#ifdef _WIN32
    const DWORD ms = static_cast<DWORD>(milliseconds);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{milliseconds / 1000, (milliseconds % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

void setNonBlocking(SocketHandle s, bool nonBlocking) {
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    ioctlsocket(s, FIONBIO, &mode);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

}  // namespace

// The work of one ping, with access to the ServerPing's private state.
struct PingRun {
    ServerPing& ping;

    void finish(PingState state, PingMessage message) {
        ping.update([&](PingSnapshot& s) {
            s.state = state;
            s.message = message;
        });
    }

    // Connects to the first address of `host:port` that answers within the time limit.
    SocketHandle connectTo(const std::string& host, int port, bool& resolved) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* list = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0 || list == nullptr) {
            resolved = false;
            return kBadSocket;
        }
        resolved = true;
        SocketHandle connected = kBadSocket;
        int tried = 0;
        for (addrinfo* a = list; a != nullptr && connected == kBadSocket && tried < 4 && !ping.cancelled_; a = a->ai_next, ++tried) {
            const SocketHandle s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == kBadSocket) continue;
            setNonBlocking(s, true);
            bool ok = connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0;
            if (!ok && wouldBlock(lastSocketError())) {
                // Wait for the connection in short slices, so a cancel is noticed.
                const std::int64_t deadline = nowMillis() + kConnectTimeoutMs;
                while (!ok && !ping.cancelled_ && nowMillis() < deadline) {
                    fd_set writable;
                    fd_set failed;
                    FD_ZERO(&writable);
                    FD_ZERO(&failed);
                    FD_SET(s, &writable);
                    FD_SET(s, &failed);
                    timeval slice{0, 100000};
                    const int ready = select(static_cast<int>(s) + 1, nullptr, &writable, &failed, &slice);
                    if (ready < 0) break;
                    if (ready == 0) continue;
                    int error = 0;
                    socklen_t length = sizeof(error);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
                    ok = error == 0 && FD_ISSET(s, &writable);
                    break;
                }
            }
            if (!ok) {
                closeSocket(s);
                continue;
            }
            setNonBlocking(s, false);
            setTimeouts(s, kReadTimeoutMs);
            connected = s;
        }
        freeaddrinfo(list);
        return connected;
    }

    void run(const std::string& host, int port, bool followSrv) {
        initNetworking();
        // The address to connect to may differ from the one in the handshake (an SRV record).
        std::string connectHost = host;
        int connectPort = port;
        if (followSrv && port == kDefaultServerPort && !isIpLiteral(host)) {
            if (const auto srv = lookupSrv(host)) {
                connectHost = srv->host;
                connectPort = srv->port;
            }
        }

        bool resolved = false;
        SocketHandle s = connectTo(connectHost, connectPort, resolved);
        if (ping.cancelled_) {
            if (s != kBadSocket) closeSocket(s);
            return;
        }
        if (s == kBadSocket) {
            finish(PingState::Unreachable, resolved ? PingMessage::CannotConnect : PingMessage::CannotResolve);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(ping.mutex_);
            ping.socket_ = static_cast<std::uintptr_t>(s);  // so cancel() can interrupt a blocked read
        }

        const bool ok = converse(s, host, port);

        {
            std::lock_guard<std::mutex> lock(ping.mutex_);
            ping.socket_ = kNoSocket;
        }
        closeSocket(s);
        if (!ok && !ping.cancelled_) finish(PingState::Unreachable, PingMessage::CannotConnect);
    }

    // The status exchange: handshake, status request, status response, ping request, pong.
    bool converse(SocketHandle s, const std::string& host, int port) {
        std::vector<unsigned char> handshake;
        writeVarInt(handshake, 0x00);                         // handshake: "intention"
        writeVarInt(handshake, static_cast<std::uint32_t>(kProtocolVersion));
        writeString(handshake, host);                         // the address as typed, not the SRV target
        handshake.push_back(static_cast<unsigned char>((port >> 8) & 0xFF));
        handshake.push_back(static_cast<unsigned char>(port & 0xFF));
        writeVarInt(handshake, 1);                            // next state: status
        if (!sendAll(s, framed(handshake)) || !sendAll(s, framed({0x00}))) return false;  // status request

        const auto response = readFrame(s);
        if (!response || response->id != 0x00) return false;
        const auto json = readJsonString(response->payload);
        if (!json) return false;
        const nlohmann::json status = nlohmann::json::parse(*json, nullptr, /*allow_exceptions=*/false);
        if (status.is_discarded() || !status.is_object()) return false;
        if (ping.cancelled_) return false;
        ping.update([&](PingSnapshot& snap) { applyStatus(status, snap); });

        const std::int64_t start = nowMillis();
        std::vector<unsigned char> request;
        writeVarInt(request, 0x01);  // ping request, carrying the client's clock
        for (int shift = 56; shift >= 0; shift -= 8) request.push_back(static_cast<unsigned char>((static_cast<std::uint64_t>(start) >> shift) & 0xFF));
        if (!sendAll(s, framed(request))) return false;
        const auto pong = readFrame(s);
        if (!pong || pong->id != 0x01) return false;  // the payload is not compared, as in the game
        const std::int64_t elapsed = nowMillis() - start;

        ping.update([&](PingSnapshot& snap) {
            snap.pingMillis = elapsed;
            snap.state = snap.protocol == kProtocolVersion ? PingState::Successful : PingState::Incompatible;
        });
        return true;
    }
};

ServerPing::~ServerPing() = default;

void ServerPing::update(const std::function<void(PingSnapshot&)>& change) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_) return;  // a cancelled ping reports nothing more
    change(snapshot_);
    ++snapshot_.revision;
}

PingSnapshot ServerPing::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void ServerPing::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
    if (socket_ != kNoSocket) shutdown(static_cast<SocketHandle>(socket_), 2);  // wakes a blocked read
}

void ServerPing::run(const std::string& address) {
    const auto parsed = parseServerAddress(address);
    if (!parsed) {
        update([](PingSnapshot& s) {
            s.state = PingState::Unreachable;
            s.message = PingMessage::CannotResolve;
        });
        return;
    }
    PingRun{*this}.run(parsed->host, parsed->port, /*followSrv=*/true);
}

PingSnapshot ServerPing::runBlocking(const std::string& host, int port) {
    ServerPing ping;
    PingRun{ping}.run(host, port, /*followSrv=*/false);
    return ping.snapshot();
}

std::shared_ptr<ServerPing> ServerPing::start(const std::string& address) {
    std::shared_ptr<ServerPing> ping(new ServerPing());
    // The game runs its pings on five threads; so does this. The semaphore is never destroyed: a ping
    // still waiting for DNS when the program exits must not find it gone.
    static std::counting_semaphore<5>* const slots = new std::counting_semaphore<5>(5);
    std::thread([ping, address] {
        slots->acquire();
        if (!ping->cancelled_) ping->run(address);
        slots->release();
    }).detach();
    return ping;
}

}  // namespace falcon
