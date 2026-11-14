#include "apex/worldgen/random.hpp"

#include <cstring>

namespace apex::worldgen {

Seed128 upgradeSeedTo128bitUnmixed(std::int64_t seed) {
    const std::uint64_t lo = static_cast<std::uint64_t>(seed) ^ 0x6A09E667F3BCC909ull;  // SILVER_RATIO_64
    const std::uint64_t hi = lo + 0x9E3779B97F4A7C15ull;                               // GOLDEN_RATIO_64
    return {lo, hi};
}

Seed128 upgradeSeedTo128bit(std::int64_t seed) {
    const Seed128 u = upgradeSeedTo128bitUnmixed(seed);
    return {mixStafford13(u.lo), mixStafford13(u.hi)};
}

namespace {

constexpr std::uint32_t kMd5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
    0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
    0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
    0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
    0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr int kMd5S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                           4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

constexpr std::uint32_t rotl32(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void md5Block(std::uint32_t state[4], const std::uint8_t* block) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<std::uint32_t>(block[i * 4]) | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 8) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 16) | (static_cast<std::uint32_t>(block[i * 4 + 3]) << 24);
    }
    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        const std::uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl32(a + f + kMd5K[i] + m[g], kMd5S[i]);
        a = tmp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

}  // namespace

std::array<std::uint8_t, 16> md5(const void* data, std::size_t size) {
    std::uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::size_t remaining = size;
    while (remaining >= 64) {
        md5Block(state, p);
        p += 64;
        remaining -= 64;
    }
    std::uint8_t tail[128] = {};
    if (remaining > 0) std::memcpy(tail, p, remaining);
    tail[remaining] = 0x80;
    const std::size_t padded = remaining < 56 ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8ull;
    for (int i = 0; i < 8; ++i) tail[padded - 8 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (8 * i));
    md5Block(state, tail);
    if (padded == 128) md5Block(state, tail + 64);
    std::array<std::uint8_t, 16> out{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) out[static_cast<std::size_t>(i * 4 + j)] = static_cast<std::uint8_t>(state[i] >> (8 * j));
    }
    return out;
}

Seed128 seedFromHashOf(std::string_view s) {
    const auto d = md5(s.data(), s.size());
    std::uint64_t lo = 0, hi = 0;
    for (std::size_t i = 0; i < 8; ++i) lo = (lo << 8) | d[i];
    for (std::size_t i = 8; i < 16; ++i) hi = (hi << 8) | d[i];
    return {lo, hi};
}

std::int64_t mthGetSeed(std::int32_t x, std::int32_t y, std::int32_t z) {
    const std::int32_t xm = static_cast<std::int32_t>(static_cast<std::uint32_t>(x) * 3129871u);  // int multiply: 32-bit wrap
    std::uint64_t l = static_cast<std::uint64_t>(static_cast<std::int64_t>(xm)) ^
                      (static_cast<std::uint64_t>(static_cast<std::int64_t>(z)) * 116129781ull) ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(y));
    l = l * l * 42317861ull + l * 11ull;
    return static_cast<std::int64_t>(l) >> 16;  // arithmetic shift
}

std::int32_t javaStringHashCode(std::string_view s) {
    std::uint32_t h = 0;
    const auto unit = [&h](std::uint32_t u) { h = h * 31u + u; };
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<std::uint8_t>(s[i]);
        std::uint32_t cp;
        std::size_t len;
        if (b0 < 0x80) {
            cp = b0;
            len = 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = (static_cast<std::uint32_t>(b0 & 0x1F) << 6) | (static_cast<std::uint8_t>(s[i + 1]) & 0x3F);
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = (static_cast<std::uint32_t>(b0 & 0x0F) << 12) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[i + 1]) & 0x3F) << 6) |
                 (static_cast<std::uint8_t>(s[i + 2]) & 0x3F);
            len = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = (static_cast<std::uint32_t>(b0 & 0x07) << 18) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[i + 1]) & 0x3F) << 12) |
                 (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[i + 2]) & 0x3F) << 6) | (static_cast<std::uint8_t>(s[i + 3]) & 0x3F);
            len = 4;
        } else {
            cp = 0xFFFD;
            len = 1;
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            unit(0xD800u + (cp >> 10));
            unit(0xDC00u + (cp & 0x3FFu));
        } else {
            unit(cp);
        }
    }
    return static_cast<std::int32_t>(h);
}

std::unique_ptr<RandomSource> XoroshiroRandomSource::fork() { return std::make_unique<XoroshiroRandomSource>(forkValue()); }

std::unique_ptr<PositionalRandomFactory> XoroshiroRandomSource::forkPositional() {
    return std::make_unique<XoroshiroPositionalRandomFactory>(forkPositionalValue());
}

std::unique_ptr<PositionalRandomFactory> LegacyRandomSource::forkPositional() {
    return std::make_unique<LegacyPositionalRandomFactory>(nextLong());
}

}  // namespace apex::worldgen
