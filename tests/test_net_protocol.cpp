// The multiplayer protocol's data: the byte reader and writer and the item stacks as the network writes them (no game data is needed).
#include "test_net_vectors.hpp"

#include "falcon/net/bytes.hpp"
#include "falcon/net/crypto.hpp"
#include "falcon/net/last_seen.hpp"
#include "falcon/net/multiplayer_client.hpp"
#include "falcon/net/server_commands.hpp"
#include "falcon/net/session_auth.hpp"
#include "falcon/net/skin_fetcher.hpp"
#include "falcon/render/mob_renderer.hpp"
#include "falcon/net/protocol_data.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {
int g_failures = 0;
}

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (false)

using namespace falcon;
using namespace falcon::net;

namespace {

void test_varints() {
    for (const int v : {0, 1, 127, 128, 255, 300, 25565, 2097151, 2147483647, -1}) {
        Writer w;
        w.varint(v);
        Reader r(w.data);
        CHECK(r.varint() == v);
        CHECK(r.ok);
        CHECK(r.left() == 0);
    }
    Writer w;
    w.varint(300);
    CHECK(w.data.size() == 2);  // 300 takes two bytes
}

void test_reader_runs_out() {
    const Bytes none;
    Reader r(none);
    (void)r.i32();
    CHECK(!r.ok);  // reading past the end is an error, not a crash
}

ComponentIds ids() {
    ComponentIds c;
    c.damage = 3;
    c.enchantments = 9;
    c.customData = 20;
    c.unbreakable = 5;
    c.glintOverride = 11;
    return c;
}

void test_item_stacks() {
    {  // nothing: a count of zero is all there is
        Writer w;
        w.varint(0);
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(s.isEmpty());
        CHECK(known);
        CHECK(r.left() == 0);
    }
    {  // a plain stack: five of item 123
        Writer w;
        w.varint(5);
        w.varint(123);
        w.varint(0);
        w.varint(0);
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(static_cast<int>(s.item) == 123);
        CHECK(s.count == 5);
        CHECK(known);
        CHECK(r.left() == 0);
    }
    {  // components that can be read are stepped over, and the damage is kept; the removed ones are only a list of numbers
        Writer w;
        w.varint(1);
        w.varint(7);
        w.varint(4);  // four components
        w.varint(2);  // and two removed
        w.varint(3);  // damage
        w.varint(17);
        w.varint(9);  // enchantments: two of them
        w.varint(2);
        w.varint(11);
        w.varint(1);
        w.varint(12);
        w.varint(3);
        w.varint(5);  // unbreakable: a unit, which is nothing
        w.varint(20);  // custom data: an empty compound
        w.u8(10);
        w.u8(0);
        w.varint(40);  // removed
        w.varint(41);
        w.varint(99);  // after the stack: something else
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(known);
        CHECK(s.damage == 17);
        CHECK(r.varint() == 99);  // the reader stands right after the stack
    }
    {  // a component that has no known layout stops the reading and says so
        Writer w;
        w.varint(2);
        w.varint(9);
        w.varint(1);
        w.varint(0);
        w.varint(77);  // not one of the components that can be read
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(!known);
        CHECK(static_cast<int>(s.item) == 9);
        CHECK(s.count == 2);
    }
    {  // what is written (without components) reads back the same
        ItemStack s(static_cast<Item>(55), 32);
        Writer w;
        writeItemStack(w, s);
        Reader r(w.data);
        bool known = true;
        const ItemStack back = readItemStack(r, ids(), known);
        CHECK(known);
        CHECK(back.item == s.item);
        CHECK(back.count == 32);
        Writer none;
        writeItemStack(none, ItemStack());
        CHECK(none.data.size() == 1 && none.data[0] == 0);  // an empty stack is one zero
    }
}

// Which of the 18 default skins a UUID gets: Java's UUID.hashCode and Math.floorMod, worked out by hand.
void test_default_skins() {
    Uuid id{};
    CHECK(defaultSkinOf(id) == 0);  // the nil UUID hashes to 0
    id[7] = 1;                      // most significant half 1
    CHECK(defaultSkinOf(id) == 1);
    id = Uuid{};
    id[15] = 0x80;  // the least significant half 0x80 (the hash's low half is then 0x80 = 128; 128 mod 18 = 2)
    CHECK(defaultSkinOf(id) == 2);
    id = Uuid{};
    for (int i = 8; i < 16; ++i) id[static_cast<std::size_t>(i)] = 0xFF;
    id[15] = 0xFE;  // the least significant half is -2: the hash is (-1) ^ (-2) = 1
    CHECK(defaultSkinOf(id) == 1);
    id = Uuid{};
    id[12] = 0x80;  // the low half of the hash is 0x80000000, which is -2147483648 as a signed number; floorMod(-2147483648, 18) is 16
    CHECK(defaultSkinOf(id) == 16);
}

// Entity data values of the kinds whose layout is known are stepped over; one of another kind stops the reading.
void test_entity_data_skipping() {
    Writer w;
    w.u8(5);  // a byte (kind 0)
    w.varint(300);  // an int (kind 1)
    w.string("abc");  // a string (kind 4)
    w.boolean(false);  // an optional component that is absent (kind 6)
    w.boolean(true);  // an optional block position that is there (kind 11)
    w.i64(123456);
    w.varint(77);  // the marker after them all
    Reader r(w.data);
    CHECK(skipEntityDataValue(0, r));
    CHECK(skipEntityDataValue(1, r));
    CHECK(skipEntityDataValue(4, r));
    CHECK(skipEntityDataValue(6, r));
    CHECK(skipEntityDataValue(11, r));
    CHECK(r.varint() == 77);
    CHECK(!skipEntityDataValue(18, r));  // villager data: not known here
}

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::string hexOf(const Bytes& b) {
    static const char* const kHex = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t v : b) {
        s += kHex[v >> 4];
        s += kHex[v & 15];
    }
    return s;
}

Bytes fromHex(const std::string& s) {
    Bytes b;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) b.push_back(static_cast<std::uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return b;
}

// The hash a login tells the session server about: the SHA-1 as a signed hexadecimal number (the three names are the well-known examples).
void test_session_hash() {
    CHECK(hexOf(sha1(bytesOf("abc"))) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(hexOf(sha256(bytesOf("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(minecraftDigestHex(sha1(bytesOf("Notch"))) == vectors::kNotchHash);
    CHECK(minecraftDigestHex(sha1(bytesOf("jeb_"))) == vectors::kJebHash);  // (the first bit is set: a minus sign)
    CHECK(minecraftDigestHex(sha1(bytesOf("simon"))) == vectors::kSimonHash);  // (it starts with a zero, which is left out)
    CHECK(sessionHash("Notch", {}, {}) == vectors::kNotchHash);
    // the server id, the secret and the key go in one after the other
    CHECK(sessionHash("No", bytesOf("tc"), bytesOf("h")) == vectors::kNotchHash);
}

// AES-128/CFB8 against another implementation, with the text in pieces (the stream goes on where it stopped) and back.
void test_cfb8() {
    const Bytes key = fromHex("2b7e151628aed2a6abf7158809cf4f3c"), iv = fromHex("000102030405060708090a0b0c0d0e0f");
    Bytes plain(70);
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<std::uint8_t>(i * 37 + 11);
    Cfb8 encrypt, decrypt;
    CHECK(encrypt.init(key.data(), iv.data()));
    CHECK(decrypt.init(key.data(), iv.data()));
    Bytes data = plain;
    encrypt.encrypt(data.data(), 1);
    encrypt.encrypt(data.data() + 1, 33);
    encrypt.encrypt(data.data() + 34, 36);
    CHECK(hexOf(data) == vectors::kCfb8Cipher);
    decrypt.decrypt(data.data(), 20);
    decrypt.decrypt(data.data() + 20, 50);
    CHECK(data == plain);
}

void test_rsa() {
    const Bytes privateKey = base64Decode(vectors::kPrivateKeyBase64), publicKey = base64Decode(vectors::kPublicKeyBase64);
    CHECK(!privateKey.empty() && !publicKey.empty());
    Bytes signature;
    std::string error;
    CHECK(rsaSign(privateKey, bytesOf("a test message that is signed"), signature, error));
    CHECK(hexOf(signature) == vectors::kSignatureHex);  // (PKCS#1 v1.5 signing gives the same bytes every time)
    CHECK(signature.size() == 256);
    {  // a chat message: the bytes the signature covers are put together the way the game does, and signed the same as by another implementation
        std::array<std::uint8_t, 16> sender{}, session{};
        for (std::size_t i = 0; i < 16; ++i) {
            sender[i] = static_cast<std::uint8_t>(i + 1);
            session[i] = static_cast<std::uint8_t>(0x80 + i);
        }
        const Bytes payload = chatSignaturePayload(sender, session, 3, 0x0123456789abcdefLL, 1700000000LL, "héllo ✓", {});
        CHECK(payload.size() == 4 + 16 + 16 + 4 + 8 + 8 + 4 + 10 + 4);  // (the text is 7 characters and 10 bytes)
        CHECK(payload[0] == 0 && payload[3] == 1);
        Bytes chatSignature;
        CHECK(rsaSign(privateKey, payload, chatSignature, error));
        CHECK(hexOf(chatSignature) == vectors::kChatSignatureHex);
        // with two messages of others seen, their signatures are in what is signed
        std::vector<std::array<std::uint8_t, 256>> seen(2);
        for (std::size_t i = 0; i < 256; ++i) {
            seen[0][i] = static_cast<std::uint8_t>(i);
            seen[1][i] = static_cast<std::uint8_t>(255 - i);
        }
        const Bytes withSeen = chatSignaturePayload(sender, session, 3, 0x0123456789abcdefLL, 1700000000LL, "héllo ✓", seen);
        CHECK(withSeen.size() == payload.size() + 2 * 256);
        Bytes seenSignature;
        CHECK(rsaSign(privateKey, withSeen, seenSignature, error));
        CHECK(hexOf(seenSignature) == vectors::kChatSignatureSeenHex);
    }
    Bytes locked;
    CHECK(rsaEncrypt(publicKey, bytesOf("0123456789abcdef"), locked, error));
    CHECK(locked.size() == 256);
    Bytes again;
    CHECK(rsaEncrypt(publicKey, bytesOf("0123456789abcdef"), again, error));
    CHECK(locked != again);  // (the padding is random)
    CHECK(!rsaEncrypt(Bytes{1, 2, 3}, bytesOf("x"), again, error));  // not a key
    if (const char* dump = std::getenv("FALCON_NET_TEST_RSA_DUMP")) {  // (for checking the lock with another implementation: the tests cannot open it)
        std::ofstream(dump) << hexOf(locked);
    }
}

void test_account_texts() {
    CHECK(base64Decode("AQID") == (Bytes{1, 2, 3}));
    CHECK(base64Decode("AQI=") == (Bytes{1, 2}));
    CHECK(base64Decode("AQ I\nD") == (Bytes{1, 2, 3}));  // (whitespace is skipped)
    CHECK(base64Decode("A*ID").empty());
    const std::string pem = "-----BEGIN RSA PRIVATE KEY-----\nAQID\nBAUG\n-----END RSA PRIVATE KEY-----\n";
    CHECK(pemBody(pem) == (Bytes{1, 2, 3, 4, 5, 6}));
    CHECK(parseIsoMillis("1970-01-01T00:00:00Z") == 0);
    CHECK(parseIsoMillis("2020-02-29T12:34:56.789012Z") == 1582979696789LL);
    CHECK(parseIsoMillis("2038-01-19T03:14:08Z") == 2147483648000LL);
    CHECK(parseIsoMillis("not a time") == -1);
    std::array<std::uint8_t, 16> id{};
    CHECK(parseUuid("069a79f4-44e9-4726-a5be-fca90e38aaf5", id));
    CHECK(id[0] == 0x06 && id[15] == 0xf5 && id[7] == 0x26);
    CHECK(uuidWithoutDashes(id) == "069a79f444e94726a5befca90e38aaf5");
    CHECK(!parseUuid("069a79f4-44e9", id));
    CHECK(!parseUuid("zz9a79f444e94726a5befca90e38aaf5", id));
    // the answer of the certificates request
    const std::string body = std::string("{\"keyPair\":{\"privateKey\":\"-----BEGIN RSA PRIVATE KEY-----\\nAQID\\n-----END RSA PRIVATE KEY-----\\n\",") +
                             "\"publicKey\":\"-----BEGIN RSA PUBLIC KEY-----\\nBAUG\\n-----END RSA PUBLIC KEY-----\\n\"}," + "\"publicKeySignature\":\"CQkJ\",\"publicKeySignatureV2\":\"BwgJ\"," +
                             "\"expiresAt\":\"2020-02-29T12:34:56.789012Z\",\"refreshedAfter\":\"2020-02-29T00:00:00Z\"}";
    ChatKeys keys;
    std::string error;
    CHECK(parseChatKeysResponse(body, keys, error));
    CHECK(keys.privateKey == (Bytes{1, 2, 3}) && keys.publicKey == (Bytes{4, 5, 6}));
    CHECK(keys.signature == (Bytes{7, 8, 9}));  // (the one that includes the player's id)
    CHECK(keys.expiresAtMillis == 1582979696789LL);
    CHECK(!parseChatKeysResponse("{}", keys, error));
}

#ifdef _WIN32
// A connection that goes encrypted in the middle: the packet that carries the key is plain, everything after it is AES-CFB8 in both directions. A thread on the other end of a
// socket on this machine plays the server with the same cipher (which is checked against another implementation above); no server is needed.
void test_encrypted_connection() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    CHECK(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    CHECK(::listen(listener, 1) == 0);
    int length = sizeof address;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
    const int port = ntohs(address.sin_port);

    const std::uint8_t secret[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 11, 12, 13, 14, 15, 16};
    bool serverSawKey = false, serverSawReply = false;
    std::thread server([&] {
        SOCKET c = ::accept(listener, nullptr, nullptr);
        const DWORD timeout = 5000;
        ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        const auto readExact = [&](std::uint8_t* out, std::size_t n) {
            std::size_t got = 0;
            while (got < n) {
                const int r = ::recv(c, reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
                if (r <= 0) return false;
                got += static_cast<std::size_t>(r);
            }
            return true;
        };
        std::uint8_t frame[64] = {};
        // 1: the key packet, as it is: its length, the id 1, the body
        if (!readExact(frame, 1) || frame[0] != 4 || !readExact(frame + 1, 4)) return;
        serverSawKey = frame[1] == 1 && frame[2] == 0xAA && frame[3] == 0xBB && frame[4] == 0xCC;
        // 2: from here on both ways are encrypted: the server sends a packet (id 7, "hello") ...
        Cfb8 send, receive;
        if (!send.init(secret, secret) || !receive.init(secret, secret)) return;
        std::uint8_t out[7] = {6, 7, 'h', 'e', 'l', 'l', 'o'};
        send.encrypt(out, sizeof out);
        ::send(c, reinterpret_cast<const char*>(out), sizeof out, 0);
        // ... and reads the answer (id 9, "world")
        std::uint8_t reply[7] = {};
        if (!readExact(reply, sizeof reply)) return;
        receive.decrypt(reply, sizeof reply);
        serverSawReply = reply[0] == 6 && reply[1] == 9 && std::string(reinterpret_cast<char*>(reply + 2), 5) == "world";
        ::closesocket(c);
    });

    Connection connection;
    std::string error;
    CHECK(connection.connect("127.0.0.1", port, error));
    CHECK(connection.sendAndEncrypt(1, Bytes{0xAA, 0xBB, 0xCC}, secret));
    CHECK(connection.encrypted());
    Packet packet;
    bool got = false;
    for (int i = 0; i < 500 && !got; ++i) {
        got = connection.poll(packet);
        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(got);
    CHECK(packet.id == 7 && std::string(packet.body.begin(), packet.body.end()) == "hello");
    connection.send(9, Bytes{'w', 'o', 'r', 'l', 'd'});
    server.join();
    connection.close();
    ::closesocket(listener);
    CHECK(serverSawKey);    // the key went out as it was, so that the server could read it
    CHECK(serverSawReply);  // and what the client sent after it was encrypted
}
#endif

// Which messages of others were seen (the game's LastSeenMessagesTracker): the ring of twenty, the bits, the checksum of the signatures, the count since the last report.
void test_last_seen() {
    LastSeenTracker tracker;
    LastSeenTracker::Update none = tracker.generateAndApplyUpdate();
    CHECK(none.offset == 0 && none.acknowledged == 0 && none.seen.empty() && none.checksum == 1);  // (the checksum of an empty list is 1)
    MessageSignature first{}, second{};
    first.fill(1);
    second.fill(2);
    CHECK(tracker.addPending(first));
    CHECK(!tracker.addPending(first));  // the same one twice counts once
    CHECK(tracker.offset() == 1);
    LastSeenTracker::Update one = tracker.generateAndApplyUpdate();
    CHECK(one.offset == 1 && one.seen.size() == 1 && one.seen[0] == first);
    CHECK(one.acknowledged == (1u << 19));  // the newest is the last of the twenty places
    CHECK(one.checksum == 32);              // worked out with another implementation (Arrays.hashCode of the bytes, then 31 * 1 + that, the low byte)
    CHECK(tracker.offset() == 0);
    CHECK(tracker.addPending(second));
    LastSeenTracker::Update two = tracker.generateAndApplyUpdate();
    CHECK(two.offset == 1 && two.seen.size() == 2 && two.seen[0] == first && two.seen[1] == second);  // the first stays acknowledged
    CHECK(two.acknowledged == ((1u << 19) | (1u << 18)));
    CHECK(two.checksum == 225);
    // twenty-one messages: the oldest falls out of the window
    LastSeenTracker many;
    for (int i = 0; i < 21; ++i) {
        MessageSignature s{};
        s.fill(static_cast<std::uint8_t>(10 + i));
        CHECK(many.addPending(s));
    }
    const LastSeenTracker::Update window = many.generateAndApplyUpdate();
    CHECK(window.offset == 21 && window.seen.size() == 20 && window.acknowledged == 0xFFFFFu);
    CHECK(window.seen[0][0] == 11 && window.seen[19][0] == 30);  // (the message 10 is gone)
}

// The skin a profile names (only a picture on the game's own texture server is followed), and an old 64x32 skin made into a 64x64 one the way the game does it.
void test_skins() {
    const auto slim = parseTexturesProperty("eyJ0aW1lc3RhbXAiOjEsInByb2ZpbGVJZCI6IngiLCJwcm9maWxlTmFtZSI6InkiLCJ0ZXh0dXJlcyI6eyJTS0lOIjp7InVybCI6Imh0dHA6Ly90ZXh0dXJlcy5taW5lY3JhZnQubmV0L3RleHR1cmUvYWJjMTIzZGVmNDU2IiwibWV0YWRhdGEiOnsibW9kZWwiOiJzbGltIn19fX0=");
    CHECK(slim && slim->hash == "abc123def456" && slim->slim);
    const auto wide = parseTexturesProperty("eyJ0ZXh0dXJlcyI6eyJTS0lOIjp7InVybCI6Imh0dHBzOi8vdGV4dHVyZXMubWluZWNyYWZ0Lm5ldC90ZXh0dXJlLzAxMjM0NTY3ODlhYmNkZWYifX19");
    CHECK(wide && wide->hash == "0123456789abcdef" && !wide->slim);  // (no metadata: the wide model)
    CHECK(!parseTexturesProperty("eyJ0ZXh0dXJlcyI6eyJTS0lOIjp7InVybCI6Imh0dHA6Ly9ldmlsLmV4YW1wbGUvdGV4dHVyZS9hYmMxMjMifX19"));  // another host
    CHECK(!parseTexturesProperty("eyJ0ZXh0dXJlcyI6eyJTS0lOIjp7InVybCI6Imh0dHA6Ly90ZXh0dXJlcy5taW5lY3JhZnQubmV0L3RleHR1cmUvLi4vLi4veCJ9fX0="));  // not a hash
    CHECK(!parseTexturesProperty("eyJ0ZXh0dXJlcyI6eyJDQVBFIjp7InVybCI6Imh0dHA6Ly90ZXh0dXJlcy5taW5lY3JhZnQubmV0L3RleHR1cmUvYWJjMTIzIn19fQ=="));  // a cape, not a skin
    CHECK(!parseTexturesProperty("not base64 at all"));

    // 64x32: every pixel is (x, y, 7) and half transparent, so that what happens to the pixels shows
    Image old;
    old.width = 64;
    old.height = 32;
    old.rgba.resize(64u * 32u * 4u);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            unsigned char* p = &old.rgba[(static_cast<std::size_t>(y) * 64u + static_cast<std::size_t>(x)) * 4u];
            p[0] = static_cast<unsigned char>(x);
            p[1] = static_cast<unsigned char>(y);
            p[2] = 7;
            p[3] = 200;
        }
    }
    const auto at = [](const Image& image, int x, int y) { return &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4u]; };
    const Image made = prepareSkin(old);
    CHECK(made.width == 64 && made.height == 64);
    // the right leg's top (4,16) 4x4 goes to (20,48) mirrored: its left edge ends up on the right
    CHECK(at(made, 23, 48)[0] == 4 && at(made, 23, 48)[1] == 16);
    CHECK(at(made, 20, 51)[0] == 7 && at(made, 20, 51)[1] == 19);
    // the right arm's front (44,20) 4x12 goes to (36,52) (offset -8, 32) mirrored
    CHECK(at(made, 39, 52)[0] == 44 && at(made, 39, 52)[1] == 20);
    // the base layers are opaque, the copies too; the hat area (all of it was opaque enough: 200 is not under 128) is made see-through
    CHECK(at(made, 10, 5)[3] == 255 && at(made, 30, 20)[3] == 255 && at(made, 23, 48)[3] == 255 && at(made, 40, 5)[3] == 0);
    // a hat area with a transparent pixel is left alone
    Image hatted = old;
    hatted.rgba[(static_cast<std::size_t>(3) * 64u + 40u) * 4u + 3u] = 50;
    const Image kept = prepareSkin(hatted);
    CHECK(at(kept, 41, 5)[3] == 200 && at(kept, 40, 3)[3] == 50);
    // a modern skin keeps its size and gets only the opaque base layers
    Image modern;
    modern.width = 64;
    modern.height = 64;
    modern.rgba.assign(64u * 64u * 4u, 100);
    const Image same = prepareSkin(modern);
    CHECK(same.height == 64 && at(same, 10, 5)[3] == 255 && at(same, 40, 5)[3] == 100 && at(same, 5, 40)[3] == 100);  // (the second layers keep their alpha)
    Image odd;
    odd.width = 128;
    odd.height = 128;
    odd.rgba.assign(128u * 128u * 4u, 255);
    CHECK(prepareSkin(odd).empty());  // (only the two sizes are known)
}

// The mobs that can be drawn (the table is checked against the game's files by the run that made it; here only what it says).
void test_mob_table() {
    CHECK(MobRenderer::supports("zombie") && MobRenderer::supports("pig") && MobRenderer::supports("sheep") && MobRenderer::supports("spider"));
    CHECK(MobRenderer::supports("arrow") && MobRenderer::supports("oak_boat") && MobRenderer::supports("bamboo_chest_raft"));
    CHECK(!MobRenderer::supports("item") && !MobRenderer::supports("player") && !MobRenderer::supports("painting") && !MobRenderer::supports(""));
    float width = 0.0f, height = 0.0f;
    CHECK(MobRenderer::sizeOf("zombie", width, height) && width == 0.6f && height == 1.95f);
    CHECK(MobRenderer::sizeOf("oak_boat", width, height) && width == 1.375f && height == 0.5625f);
    CHECK(!MobRenderer::sizeOf("item", width, height));
    CHECK(MobRenderer::attackable("zombie") && MobRenderer::attackable("oak_boat") && !MobRenderer::attackable("arrow") && !MobRenderer::attackable("item"));
    const std::vector<std::string> types = MobRenderer::supportedTypes();
    CHECK(types.size() > 40);
    for (std::size_t i = 0; i < types.size(); ++i) {
        for (std::size_t j = i + 1; j < types.size(); ++j) CHECK(types[i] != types[j]);  // (each type once)
    }
}

// A command tree as a server sends it, and which arguments of what is typed are messages to sign.
void test_server_commands() {
    Writer w;
    w.varint(6);
    const auto node = [&](int flags, std::vector<int> children, int redirect, const char* name, int parser) {
        w.u8(flags);
        w.varint(static_cast<std::int32_t>(children.size()));
        for (const int c : children) w.varint(c);
        if (flags & 8) w.varint(redirect);
        if ((flags & 3) != 0) w.string(name);
        if ((flags & 3) == 2) {
            w.varint(parser);
            if (parser == 6) w.u8(0);  // an entity: one or many, players or all
            if (parser == 1) {         // a float with a least and a most
                w.u8(3);
                w.f32(0.0f);
                w.f32(1.0f);
            }
        }
    };
    node(0, {1, 2}, -1, "", -1);           // 0: the root
    node(1, {3}, -1, "msg", -1);            // 1: msg
    node(1, {5}, -1, "say", -1);            // 2: say
    node(2, {4}, -1, "targets", 6);         // 3: <targets> (an entity)
    node(2 | 4, {}, -1, "message", 20);     // 4: <message>, a message
    node(2 | 4, {}, -1, "message", 20);     // 5: <message>
    w.varint(0);                            // the root is node 0
    ServerCommands commands;
    Reader r(w.data);
    CHECK(commands.read(r) && !commands.empty());
    auto args = commands.signableArguments("msg Steve hello world");
    CHECK(args.size() == 1 && args[0].name == "message" && args[0].value == "hello world");
    args = commands.signableArguments("say hi");
    CHECK(args.size() == 1 && args[0].value == "hi");
    args = commands.signableArguments("msg @a[team=red, limit=2] yo");  // a selector with a space in its brackets is one word
    CHECK(args.size() == 1 && args[0].value == "yo");
    CHECK(commands.signableArguments("msg Steve").empty());       // no message yet
    CHECK(commands.signableArguments("gamemode creative").empty());  // not in the tree
    CHECK(commands.signableArguments("").empty());
    // a tree that cannot be read leaves nothing behind
    Writer bad;
    bad.varint(2);
    bad.u8(0);
    bad.varint(1);
    bad.varint(5);  // a child that is not there... and the data ends
    Reader badReader(bad.data);
    ServerCommands none;
    CHECK(!none.read(badReader) && none.empty());
}

// A villager's offer as the network writes it (what it costs, what it gives, how often it was used, what moves the price) and the packet of several.
void test_trade_offers() {
    ComponentIds ids;
    ids.damage = 3;
    Writer w;
    w.varint(5);   // costA: the item,
    w.varint(3);   // how many,
    w.varint(0);   // no condition on its components
    w.varint(2);   // the result: how many (a stack says this first), then the item, and no components added or removed
    w.varint(9);
    w.varint(0);
    w.varint(0);
    w.boolean(true);  // costB
    w.varint(7);
    w.varint(1);
    w.varint(0);
    w.boolean(false);  // not out of stock
    w.i32(1);          // used once
    w.i32(12);         // of twelve
    w.i32(5);          // experience
    w.i32(-2);         // the price of this player
    w.f32(0.05f);
    w.i32(3);          // demand
    w.varint(99);      // something after
    Reader r(w.data);
    TradeOffer offer;
    CHECK(readTradeOffer(r, ids, offer));
    CHECK(static_cast<int>(offer.costA.item) == 5 && offer.costA.count == 3);
    CHECK(static_cast<int>(offer.result.item) == 9 && offer.result.count == 2);
    CHECK(static_cast<int>(offer.costB.item) == 7 && offer.costB.count == 1);
    CHECK(!offer.outOfStock && offer.uses == 1 && offer.maxUses == 12 && offer.xp == 5 && offer.specialPriceDiff == -2 && offer.demand == 3);
    CHECK(offer.priceMultiplier > 0.049f && offer.priceMultiplier < 0.051f);
    CHECK(r.varint() == 99);  // the reader stands right after the offer
    // a trade of one thing, with a condition on a component that can be read (the damage)
    Writer one;
    one.varint(4);
    one.varint(1);
    one.varint(1);  // one condition: the damage is 17
    one.varint(3);
    one.varint(17);
    one.varint(1);  // the result: 1 of item 8
    one.varint(8);
    one.varint(0);
    one.varint(0);
    one.boolean(false);  // no second cost
    one.boolean(true);   // out of stock
    for (int i = 0; i < 4; ++i) one.i32(0);
    one.f32(0.0f);
    one.i32(0);
    Reader oneReader(one.data);
    TradeOffer second;
    CHECK(readTradeOffer(oneReader, ids, second));
    CHECK(second.costB.isEmpty() && second.outOfStock && static_cast<int>(second.result.item) == 8);
    // a cost with a condition that cannot be read is refused (nothing more can be told after it)
    Writer bad;
    bad.varint(4);
    bad.varint(1);
    bad.varint(1);
    bad.varint(77);
    Reader badReader(bad.data);
    TradeOffer none;
    CHECK(!readTradeOffer(badReader, ids, none));
}

}  // namespace

int main() {
    test_varints();
    test_reader_runs_out();
    test_item_stacks();
    test_default_skins();
    test_entity_data_skipping();
    test_session_hash();
    test_cfb8();
    test_rsa();
    test_account_texts();
    test_last_seen();
    test_skins();
    test_mob_table();
    test_server_commands();
    test_trade_offers();
#ifdef _WIN32
    test_encrypted_connection();
#endif
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "net protocol tests passed\n";
    return 0;
}
