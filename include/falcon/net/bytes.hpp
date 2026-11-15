#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace falcon::net {

using Bytes = std::vector<std::uint8_t>;

// Writes what the game's FriendlyByteBuf writes: big-endian numbers, VarInts, strings with their length in front.
class Writer {
public:
    Bytes data;
    void u8(int v) { data.push_back(static_cast<std::uint8_t>(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void i16(int v) { u8(v >> 8); u8(v); }
    void i32(std::int32_t v) { for (int s = 24; s >= 0; s -= 8) u8(v >> s); }
    void i64(std::int64_t v) { for (int s = 56; s >= 0; s -= 8) u8(static_cast<int>(v >> s)); }
    void f32(float v) { std::uint32_t b; std::memcpy(&b, &v, 4); i32(static_cast<std::int32_t>(b)); }
    void f64(double v) { std::uint64_t b; std::memcpy(&b, &v, 8); i64(static_cast<std::int64_t>(b)); }
    void varint(std::int32_t value) {
        std::uint32_t v = static_cast<std::uint32_t>(value);
        while (v >= 0x80) { u8(static_cast<int>((v & 0x7F) | 0x80)); v >>= 7; }
        u8(static_cast<int>(v));
    }
    void string(std::string_view s) { varint(static_cast<std::int32_t>(s.size())); data.insert(data.end(), s.begin(), s.end()); }
    void uuid(const std::array<std::uint8_t, 16>& id) { data.insert(data.end(), id.begin(), id.end()); }
    void raw(const Bytes& b) { data.insert(data.end(), b.begin(), b.end()); }
};

// Reads the same. Reading past the end sets `ok` to false and gives zeros, so a short packet cannot run off the buffer.
class Reader {
public:
    Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
    explicit Reader(const Bytes& b) : p_(b.data()), n_(b.size()) {}
    bool ok = true;
    [[nodiscard]] std::size_t pos() const { return pos_; }
    [[nodiscard]] std::size_t left() const { return n_ - pos_; }
    int u8() { if (pos_ >= n_) { ok = false; return 0; } return p_[pos_++]; }
    bool boolean() { return u8() != 0; }
    int i8() { return static_cast<std::int8_t>(u8()); }
    int i16() { const int hi = u8(); return static_cast<std::int16_t>((hi << 8) | u8()); }
    int u16() { const int hi = u8(); return (hi << 8) | u8(); }
    std::int32_t i32() { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<std::uint32_t>(u8()); return static_cast<std::int32_t>(v); }
    std::int64_t i64() { std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint64_t>(u8()); return static_cast<std::int64_t>(v); }
    float f32() { const std::uint32_t b = static_cast<std::uint32_t>(i32()); float v; std::memcpy(&v, &b, 4); return v; }
    double f64() { const std::uint64_t b = static_cast<std::uint64_t>(i64()); double v; std::memcpy(&v, &b, 8); return v; }
    std::int32_t varint() {
        std::uint32_t v = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            const int b = u8();
            v |= static_cast<std::uint32_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            if (!ok) break;
        }
        return static_cast<std::int32_t>(v);
    }
    std::int64_t varlong() {
        std::uint64_t v = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            const int b = u8();
            v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            if (!ok) break;
        }
        return static_cast<std::int64_t>(v);
    }
    std::string string() {
        const int n = varint();
        if (n < 0 || static_cast<std::size_t>(n) > left()) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p_ + pos_), static_cast<std::size_t>(n));
        pos_ += static_cast<std::size_t>(n);
        return s;
    }
    std::array<std::uint8_t, 16> uuid() { std::array<std::uint8_t, 16> id{}; for (auto& b : id) b = static_cast<std::uint8_t>(u8()); return id; }
    void skip(std::size_t n) { if (n > left()) { ok = false; pos_ = n_; } else pos_ += n; }
    const std::uint8_t* here() const { return p_ + pos_; }

private:
    const std::uint8_t* p_;
    std::size_t n_;
    std::size_t pos_ = 0;
};

}  // namespace falcon::net
