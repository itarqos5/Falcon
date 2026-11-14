#include "falcon/worldgen/legacy_noise.hpp"

#include "falcon/worldgen/java_math.hpp"

#include <algorithm>
#include <cmath>

namespace falcon::worldgen {

namespace {

constexpr int kGradient[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                                  {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};

const double kSqrt3 = std::sqrt(3.0);
const double kF2 = 0.5 * (kSqrt3 - 1.0);
const double kG2 = (3.0 - kSqrt3) / 6.0;

[[nodiscard]] double cornerNoise(int gradientIndex, double x, double y, double z, double offset) {
    double d = offset - x * x - y * y - z * z;
    double d1;
    if (d < 0.0) {
        d1 = 0.0;
    } else {
        d *= d;
        const int* g = kGradient[gradientIndex];
        d1 = d * d * (g[0] * x + g[1] * y + g[2] * z);
    }
    return d1;
}

}  // namespace

SimplexNoise::SimplexNoise(RandomSource& random) {
    xo = random.nextDouble() * 256.0;
    yo = random.nextDouble() * 256.0;
    zo = random.nextDouble() * 256.0;
    for (int i = 0; i < 256; ++i) perm_[i] = i;
    for (int i = 0; i < 256; ++i) {
        const int j = random.nextInt(256 - i);
        const int k = perm_[i];
        perm_[i] = perm_[j + i];
        perm_[j + i] = k;
    }
}

double SimplexNoise::getValue(double x, double y) const {
    const double d = (x + y) * kF2;
    const int i = javaFloor(x + d);
    const int j = javaFloor(y + d);
    const double d1 = static_cast<double>(i + j) * kG2;
    const double d2 = static_cast<double>(i) - d1;
    const double d3 = static_cast<double>(j) - d1;
    const double d4 = x - d2;
    const double d5 = y - d3;
    int k, l;
    if (d4 > d5) {
        k = 1;
        l = 0;
    } else {
        k = 0;
        l = 1;
    }
    const double d6 = d4 - static_cast<double>(k) + kG2;
    const double d7 = d5 - static_cast<double>(l) + kG2;
    const double d8 = d4 - 1.0 + 2.0 * kG2;
    const double d9 = d5 - 1.0 + 2.0 * kG2;
    const int i1 = i & 0xFF;
    const int j1 = j & 0xFF;
    const int k1 = p(i1 + p(j1)) % 12;
    const int l1 = p(i1 + k + p(j1 + l)) % 12;
    const int i2 = p(i1 + 1 + p(j1 + 1)) % 12;
    const double d10 = cornerNoise(k1, d4, d5, 0.0, 0.5);
    const double d11 = cornerNoise(l1, d6, d7, 0.0, 0.5);
    const double d12 = cornerNoise(i2, d8, d9, 0.0, 0.5);
    return 70.0 * (d10 + d11 + d12);
}

PerlinSimplexNoise::PerlinSimplexNoise(RandomSource& random, const std::vector<int>& octaves) {
    std::vector<int> sorted = octaves;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    const auto contains = [&](int o) { return std::binary_search(sorted.begin(), sorted.end(), o); };
    const int first = sorted.front();
    const int last = sorted.back();  // <= 0 for every stack the game defines
    const int i = -first;
    const int j = last;
    const int k = i + j + 1;
    auto base = std::make_unique<SimplexNoise>(random);
    levels_.resize(static_cast<std::size_t>(k));
    if (j >= 0 && j < k && contains(0)) levels_[static_cast<std::size_t>(j)] = std::move(base);
    for (int i1 = j + 1; i1 < k; ++i1) {
        if (i1 >= 0 && contains(j - i1)) levels_[static_cast<std::size_t>(i1)] = std::make_unique<SimplexNoise>(random);
        else random.consumeCount(262);
    }
    highestFreqInputFactor_ = std::pow(2.0, static_cast<double>(j));
    highestFreqValueFactor_ = 1.0 / (std::pow(2.0, static_cast<double>(k)) - 1.0);
}

double PerlinSimplexNoise::getValue(double x, double y, bool useNoiseOffsets) const {
    double sum = 0.0;
    double freq = highestFreqInputFactor_;
    double factor = highestFreqValueFactor_;
    for (const auto& level : levels_) {
        if (level) {
            sum += level->getValue(x * freq + (useNoiseOffsets ? level->xo : 0.0), y * freq + (useNoiseOffsets ? level->yo : 0.0)) * factor;
        }
        freq /= 2.0;
        factor *= 2.0;
    }
    return sum;
}

namespace {
PerlinSimplexNoise makeStack(std::int64_t seed, const std::vector<int>& octaves) {
    LegacyRandomSource random(seed);
    return PerlinSimplexNoise(random, octaves);
}
}  // namespace

BiomeTemperatureNoise::BiomeTemperatureNoise() : temperature_(makeStack(1234, {0})), frozen_(makeStack(3456, {-2, -1, 0})), info_(makeStack(2345, {0})) {}

const BiomeTemperatureNoise& BiomeTemperatureNoise::instance() {
    static const BiomeTemperatureNoise noise;
    return noise;
}

float BiomeTemperatureNoise::frozen(int x, int z, float baseTemperature) const {
    const double a = frozen_.getValue(static_cast<double>(x) * 0.05, static_cast<double>(z) * 0.05, false) * 7.0;
    const double b = info_.getValue(static_cast<double>(x) * 0.2, static_cast<double>(z) * 0.2, false);
    if (a + b < 0.3) {
        const double c = info_.getValue(static_cast<double>(x) * 0.09, static_cast<double>(z) * 0.09, false);
        if (c < 0.8) return 0.2f;
    }
    return baseTemperature;
}

float BiomeTemperatureNoise::lapse(float temperature, int x, int y, int z, int seaLevel) const {
    if (y > seaLevel + 17) {
        const float f = static_cast<float>(temperature_.getValue(static_cast<double>(static_cast<float>(x) / 8.0f), static_cast<double>(static_cast<float>(z) / 8.0f), false) * 8.0);
        return temperature - (f + static_cast<float>(y) - static_cast<float>(seaLevel + 17)) * 0.05f / 40.0f;
    }
    return temperature;
}

}  // namespace falcon::worldgen

namespace falcon::worldgen {

double BiomeTemperatureNoise::biomeInfo(double x, double z) const { return info_.getValue(x, z, false); }

}  // namespace falcon::worldgen
