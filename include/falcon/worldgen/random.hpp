#pragma once

// Mojang's random sources, bit for bit (26.3): the Xoroshiro128++ generator that every modern worldgen
// stage uses, the 48-bit LCG ("legacy") source, and the positional factories that derive a fresh source
// from a name, a seed or a block position.
//
// Java integer semantics are reproduced with unsigned arithmetic (wrap-around) and explicit casts.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace falcon::worldgen {

[[nodiscard]] constexpr std::uint64_t rotl64(std::uint64_t v, int n) { return (v << n) | (v >> (64 - n)); }

// RandomSupport
[[nodiscard]] constexpr std::uint64_t mixStafford13(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

struct Seed128 {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

[[nodiscard]] Seed128 upgradeSeedTo128bitUnmixed(std::int64_t seed);
[[nodiscard]] Seed128 upgradeSeedTo128bit(std::int64_t seed);  // mixStafford13 of both halves
// Guava md5(utf8(s)): bytes 0..7 and 8..15 read big-endian.
[[nodiscard]] Seed128 seedFromHashOf(std::string_view s);

// MD5 (RFC 1321) of a byte string.
[[nodiscard]] std::array<std::uint8_t, 16> md5(const void* data, std::size_t size);

// Mth.getSeed(x, y, z): the seed of the block-position random sources.
[[nodiscard]] std::int64_t mthGetSeed(std::int32_t x, std::int32_t y, std::int32_t z);

// java.lang.String.hashCode over UTF-16 code units of the UTF-8 text `s`.
[[nodiscard]] std::int32_t javaStringHashCode(std::string_view s);

class RandomSource;
class PositionalRandomFactory;

// ---------------------------------------------------------------------------------------------------------------
// The interface (RandomSource). Only what the terrain stages use; nextGaussian is not needed by any of them.
class RandomSource {
public:
    virtual ~RandomSource() = default;
    virtual void setSeed(std::int64_t seed) = 0;
    [[nodiscard]] virtual std::int32_t nextInt() = 0;
    // Uniform in [0, bound); bound must be positive (a non-positive bound returns 0).
    [[nodiscard]] virtual std::int32_t nextInt(std::int32_t bound) = 0;
    [[nodiscard]] virtual std::int64_t nextLong() = 0;
    [[nodiscard]] virtual bool nextBoolean() = 0;
    [[nodiscard]] virtual float nextFloat() = 0;
    [[nodiscard]] virtual double nextDouble() = 0;
    [[nodiscard]] virtual std::unique_ptr<RandomSource> fork() = 0;
    [[nodiscard]] virtual std::unique_ptr<PositionalRandomFactory> forkPositional() = 0;

    // origin + nextInt(bound - origin)
    [[nodiscard]] std::int32_t nextInt(std::int32_t origin, std::int32_t bound) {
        return origin >= bound ? origin : origin + nextInt(bound - origin);
    }
    [[nodiscard]] std::int32_t nextIntBetweenInclusive(std::int32_t lo, std::int32_t hi) { return nextInt(hi - lo + 1) + lo; }
    void consumeCount(int n) {
        for (int i = 0; i < n; ++i) (void)nextInt();
    }
};

class PositionalRandomFactory {
public:
    virtual ~PositionalRandomFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<RandomSource> fromHashOf(std::string_view name) const = 0;
    [[nodiscard]] virtual std::unique_ptr<RandomSource> fromSeed(std::int64_t seed) const = 0;
    [[nodiscard]] virtual std::unique_ptr<RandomSource> at(std::int32_t x, std::int32_t y, std::int32_t z) const = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// Xoroshiro128++
class Xoroshiro128PlusPlus {
public:
    constexpr Xoroshiro128PlusPlus() = default;
    constexpr Xoroshiro128PlusPlus(std::uint64_t lo, std::uint64_t hi) : lo_(lo), hi_(hi) {
        if ((lo_ | hi_) == 0) {  // the all-zero state is a fixed point: Mojang substitutes the golden/silver ratios
            lo_ = 0x9E3779B97F4A7C15ull;
            hi_ = 0x6A09E667F3BCC909ull;
        }
    }
    [[nodiscard]] constexpr std::uint64_t nextLong() {
        const std::uint64_t s0 = lo_;
        std::uint64_t s1 = hi_;
        const std::uint64_t result = rotl64(s0 + s1, 17) + s0;
        s1 ^= s0;
        lo_ = rotl64(s0, 49) ^ s1 ^ (s1 << 21);
        hi_ = rotl64(s1, 28);
        return result;
    }
    [[nodiscard]] constexpr std::uint64_t lo() const { return lo_; }
    [[nodiscard]] constexpr std::uint64_t hi() const { return hi_; }

private:
    std::uint64_t lo_ = 0x9E3779B97F4A7C15ull;
    std::uint64_t hi_ = 0x6A09E667F3BCC909ull;
};

class XoroshiroPositionalRandomFactory;

class XoroshiroRandomSource final : public RandomSource {
public:
    using RandomSource::nextInt;  // keep nextInt(origin, bound) visible next to the overrides
    explicit XoroshiroRandomSource(std::int64_t seed) { setSeed(seed); }          // seed is mixed (upgradeSeedTo128bit)
    XoroshiroRandomSource(std::uint64_t lo, std::uint64_t hi) : gen_(lo, hi) {}   // raw state, not mixed
    explicit XoroshiroRandomSource(Seed128 s) : gen_(s.lo, s.hi) {}

    void setSeed(std::int64_t seed) override {
        const Seed128 s = upgradeSeedTo128bit(seed);
        gen_ = Xoroshiro128PlusPlus(s.lo, s.hi);
    }
    [[nodiscard]] std::int64_t nextLong() override { return static_cast<std::int64_t>(gen_.nextLong()); }
    [[nodiscard]] std::int32_t nextInt() override { return static_cast<std::int32_t>(static_cast<std::uint32_t>(gen_.nextLong())); }
    [[nodiscard]] std::int32_t nextInt(std::int32_t bound) override {
        if (bound <= 0) return 0;
        std::uint64_t l = static_cast<std::uint32_t>(gen_.nextLong());
        std::uint64_t m = l * static_cast<std::uint64_t>(bound);
        std::uint64_t n = m & 0xFFFFFFFFull;
        if (n < static_cast<std::uint64_t>(bound)) {
            const std::uint32_t threshold = (~static_cast<std::uint32_t>(bound) + 1u) % static_cast<std::uint32_t>(bound);
            while (n < threshold) {
                l = static_cast<std::uint32_t>(gen_.nextLong());
                m = l * static_cast<std::uint64_t>(bound);
                n = m & 0xFFFFFFFFull;
            }
        }
        return static_cast<std::int32_t>(m >> 32);
    }
    [[nodiscard]] bool nextBoolean() override { return (gen_.nextLong() & 1ull) != 0; }
    [[nodiscard]] float nextFloat() override { return static_cast<float>(gen_.nextLong() >> 40) * 5.9604645E-8f; }
    [[nodiscard]] double nextDouble() override { return static_cast<double>(gen_.nextLong() >> 11) * 1.1102230246251565E-16; }
    [[nodiscard]] std::unique_ptr<RandomSource> fork() override;
    [[nodiscard]] std::unique_ptr<PositionalRandomFactory> forkPositional() override;

    // By-value variants (no allocation): the same as fork() / forkPositional().
    [[nodiscard]] XoroshiroRandomSource forkValue() {
        const std::uint64_t lo = gen_.nextLong();
        const std::uint64_t hi = gen_.nextLong();
        return XoroshiroRandomSource(lo, hi);
    }
    [[nodiscard]] XoroshiroPositionalRandomFactory forkPositionalValue();

private:
    Xoroshiro128PlusPlus gen_;
};

class XoroshiroPositionalRandomFactory final : public PositionalRandomFactory {
public:
    XoroshiroPositionalRandomFactory() = default;
    XoroshiroPositionalRandomFactory(std::uint64_t lo, std::uint64_t hi) : lo_(lo), hi_(hi) {}

    // By-value (no allocation) forms.
    [[nodiscard]] XoroshiroRandomSource makeFromHashOf(std::string_view name) const {
        const Seed128 h = seedFromHashOf(name);
        return XoroshiroRandomSource(h.lo ^ lo_, h.hi ^ hi_);
    }
    [[nodiscard]] XoroshiroRandomSource makeFromSeed(std::int64_t seed) const {
        const auto s = static_cast<std::uint64_t>(seed);
        return XoroshiroRandomSource(s ^ lo_, s ^ hi_);
    }
    [[nodiscard]] XoroshiroRandomSource makeAt(std::int32_t x, std::int32_t y, std::int32_t z) const {
        return XoroshiroRandomSource(static_cast<std::uint64_t>(mthGetSeed(x, y, z)) ^ lo_, hi_);  // only the low half is mixed
    }

    [[nodiscard]] std::unique_ptr<RandomSource> fromHashOf(std::string_view name) const override {
        return std::make_unique<XoroshiroRandomSource>(makeFromHashOf(name));
    }
    [[nodiscard]] std::unique_ptr<RandomSource> fromSeed(std::int64_t seed) const override {
        return std::make_unique<XoroshiroRandomSource>(makeFromSeed(seed));
    }
    [[nodiscard]] std::unique_ptr<RandomSource> at(std::int32_t x, std::int32_t y, std::int32_t z) const override {
        return std::make_unique<XoroshiroRandomSource>(makeAt(x, y, z));
    }
    [[nodiscard]] std::uint64_t lo() const { return lo_; }
    [[nodiscard]] std::uint64_t hi() const { return hi_; }

private:
    std::uint64_t lo_ = 0;
    std::uint64_t hi_ = 0;
};

inline XoroshiroPositionalRandomFactory XoroshiroRandomSource::forkPositionalValue() {
    const std::uint64_t lo = gen_.nextLong();
    const std::uint64_t hi = gen_.nextLong();
    return XoroshiroPositionalRandomFactory(lo, hi);
}

// ---------------------------------------------------------------------------------------------------------------
// The 48-bit linear congruential generator (java.util.Random's algorithm) = Mojang's LegacyRandomSource /
// ThreadSafeLegacyRandomSource / SingleThreadedRandomSource. Used by the Nether, End and cave noise settings
// (`legacy_random_source: true`) and by RandomSource.create(long).
class LegacyRandomSource final : public RandomSource {
public:
    using RandomSource::nextInt;
    explicit LegacyRandomSource(std::int64_t seed) { setSeed(seed); }

    void setSeed(std::int64_t seed) override { seed_ = (static_cast<std::uint64_t>(seed) ^ kMultiplier) & kMask; }
    [[nodiscard]] std::int32_t nextInt() override { return next(32); }
    [[nodiscard]] std::int32_t nextInt(std::int32_t bound) override {
        if (bound <= 0) return 0;
        if ((bound & (bound - 1)) == 0) return static_cast<std::int32_t>((static_cast<std::int64_t>(bound) * static_cast<std::int64_t>(next(31))) >> 31);
        std::int32_t bits;
        std::int32_t val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (static_cast<std::int32_t>(static_cast<std::uint32_t>(bits) - static_cast<std::uint32_t>(val) +
                                           static_cast<std::uint32_t>(bound - 1)) < 0);
        return val;
    }
    [[nodiscard]] std::int64_t nextLong() override {
        const std::int32_t a = next(32);
        const std::int32_t b = next(32);
        return static_cast<std::int64_t>((static_cast<std::uint64_t>(static_cast<std::int64_t>(a)) << 32) +
                                         static_cast<std::uint64_t>(static_cast<std::int64_t>(b)));
    }
    [[nodiscard]] bool nextBoolean() override { return next(1) != 0; }
    [[nodiscard]] float nextFloat() override { return static_cast<float>(next(24)) * 5.9604645E-8f; }
    [[nodiscard]] double nextDouble() override {
        const std::int64_t a = next(26);
        const std::int64_t b = next(27);
        return static_cast<double>(static_cast<std::int64_t>((static_cast<std::uint64_t>(a) << 27) + static_cast<std::uint64_t>(b))) *
               1.1102230246251565E-16;
    }
    [[nodiscard]] std::unique_ptr<RandomSource> fork() override { return std::make_unique<LegacyRandomSource>(nextLong()); }
    [[nodiscard]] std::unique_ptr<PositionalRandomFactory> forkPositional() override;

private:
    static constexpr std::uint64_t kMultiplier = 0x5DEECE66Dull;
    static constexpr std::uint64_t kMask = (1ull << 48) - 1;
    [[nodiscard]] std::int32_t next(int bits) {
        seed_ = (seed_ * kMultiplier + 11ull) & kMask;
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(seed_ >> (48 - bits)));
    }
    std::uint64_t seed_ = 0;
};

class LegacyPositionalRandomFactory final : public PositionalRandomFactory {
public:
    explicit LegacyPositionalRandomFactory(std::int64_t seed) : seed_(seed) {}
    [[nodiscard]] std::unique_ptr<RandomSource> fromHashOf(std::string_view name) const override {
        return std::make_unique<LegacyRandomSource>(static_cast<std::int64_t>(javaStringHashCode(name)) ^ seed_);
    }
    [[nodiscard]] std::unique_ptr<RandomSource> fromSeed(std::int64_t seed) const override { return std::make_unique<LegacyRandomSource>(seed); }
    [[nodiscard]] std::unique_ptr<RandomSource> at(std::int32_t x, std::int32_t y, std::int32_t z) const override {
        return std::make_unique<LegacyRandomSource>(mthGetSeed(x, y, z) ^ seed_);
    }

private:
    std::int64_t seed_;
};

// A random source of either kind, by value: what a surface rule reads a few numbers from at a position (no allocation, unlike PositionalRandomFactory::at).
class AnyRandom {
public:
    explicit AnyRandom(XoroshiroRandomSource r) : legacy_(false), x_(std::move(r)), l_(0) {}
    explicit AnyRandom(LegacyRandomSource r) : legacy_(true), x_(0), l_(std::move(r)) {}
    [[nodiscard]] float nextFloat() { return legacy_ ? l_.nextFloat() : x_.nextFloat(); }
    [[nodiscard]] double nextDouble() { return legacy_ ? l_.nextDouble() : x_.nextDouble(); }
    [[nodiscard]] std::int32_t nextInt(std::int32_t bound) { return legacy_ ? l_.nextInt(bound) : x_.nextInt(bound); }

private:
    bool legacy_;
    XoroshiroRandomSource x_;
    LegacyRandomSource l_;
};

// A positional factory of either kind, by value: the Xoroshiro one of the Overworld, or the legacy one (LegacyPositionalRandomFactory) of the Nether and the End.
class AnyPositionalFactory {
public:
    AnyPositionalFactory() = default;
    explicit AnyPositionalFactory(XoroshiroPositionalRandomFactory f) : x_(f) {}
    explicit AnyPositionalFactory(std::int64_t legacySeed) : legacy_(true), legacySeed_(legacySeed) {}
    [[nodiscard]] bool isLegacy() const { return legacy_; }
    [[nodiscard]] AnyRandom makeAt(std::int32_t x, std::int32_t y, std::int32_t z) const {
        return legacy_ ? AnyRandom(LegacyRandomSource(mthGetSeed(x, y, z) ^ legacySeed_)) : AnyRandom(x_.makeAt(x, y, z));
    }
    [[nodiscard]] AnyRandom makeFromHashOf(std::string_view name) const {
        return legacy_ ? AnyRandom(LegacyRandomSource(static_cast<std::int64_t>(javaStringHashCode(name)) ^ legacySeed_)) : AnyRandom(x_.makeFromHashOf(name));
    }

private:
    XoroshiroPositionalRandomFactory x_;
    bool legacy_ = false;
    std::int64_t legacySeed_ = 0;
};

}  // namespace falcon::worldgen
