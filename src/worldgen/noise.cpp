#include "apex/worldgen/noise.hpp"

#include "apex/worldgen/java_math.hpp"

#include <bit>
#include <climits>
#include <cmath>
#include <utility>

namespace apex::worldgen {

namespace {

// GradientNoise.GRADIENT, as floats (every entry has exactly two non-zero components).
constexpr float kGrad[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                                {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};

constexpr double kHalfRoundOff = std::bit_cast<double>(0x416FFFFFFFFFFFFFull);  // Math.nextDown(16777216.0)
constexpr double kRoundOff = 33554432.0;                                         // 2^25

// GradientNoise.wrap (the Farlands are off).
[[nodiscard]] inline double wrapCoord(double d) {
    if (d >= -kHalfRoundOff && d < kHalfRoundOff) return d;
    return d - std::floor(d / kRoundOff + 0.5) * kRoundOff;
}

// Mth.smoothstep(float): t^3 * (t * (t * 6 - 15) + 10)
[[nodiscard]] inline float smoothstep(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

[[nodiscard]] inline float gradDot(std::uint32_t hash, float x, float y, float z) {
    const float* g = kGrad[hash & 15u];
    return g[0] * x + g[1] * y + g[2] * z;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
PerlinNoise::PerlinNoise(RandomSource& random) {
    ox_ = random.nextDouble() * 256.0;
    oy_ = random.nextDouble() * 256.0;
    oz_ = random.nextDouble() * 256.0;
    for (int i = 0; i < 256; ++i) perm_[i] = static_cast<std::uint8_t>(i);
    for (int i = 0; i < 256; ++i) {
        const int j = random.nextInt(256 - i);
        const std::uint8_t b = perm_[i];
        perm_[i] = perm_[i + j];
        perm_[i + j] = b;
    }
}

float PerlinNoise::sampleAndLerp(std::int32_t xi, std::int32_t yi, std::int32_t zi, float xf, float yf, float zf, float yfOriginal) const {
    // All index sums are 32-bit wrapping adds; only the low 8 bits are used.
    const auto ux = static_cast<std::uint32_t>(xi);
    const auto uy = static_cast<std::uint32_t>(yi);
    const auto uz = static_cast<std::uint32_t>(zi);
    const std::uint32_t px0 = perm_[ux & 255u];
    const std::uint32_t px1 = perm_[(ux + 1u) & 255u];
    const std::uint32_t p00 = perm_[(px0 + uy) & 255u];
    const std::uint32_t p01 = perm_[(px0 + uy + 1u) & 255u];
    const std::uint32_t p10 = perm_[(px1 + uy) & 255u];
    const std::uint32_t p11 = perm_[(px1 + uy + 1u) & 255u];
    const float xf1 = xf - 1.0f;
    const float yf1 = yf - 1.0f;
    const float zf1 = zf - 1.0f;
    const float d000 = gradDot(perm_[(p00 + uz) & 255u], xf, yf, zf);
    const float d100 = gradDot(perm_[(p10 + uz) & 255u], xf1, yf, zf);
    const float d010 = gradDot(perm_[(p01 + uz) & 255u], xf, yf1, zf);
    const float d110 = gradDot(perm_[(p11 + uz) & 255u], xf1, yf1, zf);
    const float d001 = gradDot(perm_[(p00 + uz + 1u) & 255u], xf, yf, zf1);
    const float d101 = gradDot(perm_[(p10 + uz + 1u) & 255u], xf1, yf, zf1);
    const float d011 = gradDot(perm_[(p01 + uz + 1u) & 255u], xf, yf1, zf1);
    const float d111 = gradDot(perm_[(p11 + uz + 1u) & 255u], xf1, yf1, zf1);
    const float sx = smoothstep(xf);
    const float sy = smoothstep(yfOriginal);
    const float sz = smoothstep(zf);
    // Mth.lerp3(sx, sy, sz, ...) = lerp(sz, lerp2(sx, sy, c000, c100, c010, c110), lerp2(sx, sy, c001, c101, c011, c111))
    const float l0 = lerpf(sy, lerpf(sx, d000, d100), lerpf(sx, d010, d110));
    const float l1 = lerpf(sy, lerpf(sx, d001, d101), lerpf(sx, d011, d111));
    return lerpf(sz, l0, l1);
}

float PerlinNoise::get(double x, double y, double z) const {
    const double dx = wrapCoord(x) + ox_;
    const double dy = wrapCoord(y) + oy_;
    const double dz = wrapCoord(z) + oz_;
    const std::int32_t ix = javaFloor(dx);
    const std::int32_t iy = javaFloor(dy);
    const std::int32_t iz = javaFloor(dz);
    const float fx = static_cast<float>(dx - static_cast<double>(ix));
    const float fy = static_cast<float>(dy - static_cast<double>(iy));
    const float fz = static_cast<float>(dz - static_cast<double>(iz));
    return sampleAndLerp(ix, iy, iz, fx, fy, fz, fy);
}

float SmearedPerlinNoise::get(double x, double y, double z) const {
    const double dx = wrapCoord(x) + ox_;
    const double dy = wrapCoord(y) + oy_;
    const double dz = wrapCoord(z) + oz_;
    const std::int32_t ix = javaFloor(dx);
    const std::int32_t iy = javaFloor(dy);
    const std::int32_t iz = javaFloor(dz);
    const float fx = static_cast<float>(dx - static_cast<double>(ix));
    const double yfrac = dy - static_cast<double>(iy);
    const float fz = static_cast<float>(dz - static_cast<double>(iz));
    // computeFudgeY uses the RAW y argument, not the wrapped and offset one.
    const double base = (y >= 0.0 && y < yfrac) ? y : yfrac;
    const double fudge = static_cast<double>(javaFloor(base / fudgeYScale_ + static_cast<double>(1.0E-7f))) * fudgeYScale_;
    const float fyFudged = static_cast<float>(yfrac - fudge);
    return sampleAndLerp(ix, iy, iz, fx, fyFudged, fz, static_cast<float>(yfrac));
}

// ---------------------------------------------------------------------------------------------------------------
namespace {

bool getDoubleField(const JsonValue& o, const char* key, double& out, bool& present, std::string& error) {
    present = false;
    const JsonValue* v = o.find(key);
    if (!v) return true;
    if (!v->toDouble(out)) {
        error = std::string("field '") + key + "' is not a number";
        return false;
    }
    present = true;
    return true;
}

}  // namespace

bool parseNoiseParameters(const JsonValue& json, NoiseParameters& out, std::string& error) {
    if (!json.isObject()) {
        error = "noise parameters: not an object";
        return false;
    }
    NoiseParameters p;
    const JsonValue* bo = json.find("base_octave");
    std::int32_t baseOctave = 0;
    if (!bo || !bo->toInt(baseOctave) || baseOctave < -32 || baseOctave > 32) {
        error = "noise parameters: base_octave missing or out of range [-32, 32]";
        return false;
    }
    p.baseOctave = baseOctave;
    if (const JsonValue* oc = json.find("octave_count")) {
        std::int32_t n = 0;
        if (!oc->toInt(n) || n < 1 || n > 32) {
            error = "noise parameters: octave_count out of range [1, 32]";
            return false;
        }
        p.octaveCount = n;
    }
    bool present = false;
    double amp = 1.0;
    if (!getDoubleField(json, "base_amplitude", amp, present, error)) return false;
    if (present) {
        if (!(amp >= 9.999999747378752E-6 && amp <= 1000000.0)) {
            error = "noise parameters: base_amplitude out of range";
            return false;
        }
        p.baseAmplitude = amp;
    }
    if (const JsonValue* mods = json.find("amplitude_modifiers")) {
        if (!mods->isArray() || mods->items.size() > 32) {
            error = "noise parameters: amplitude_modifiers must be a list of at most 32 numbers";
            return false;
        }
        for (const JsonValue& m : mods->items) {
            double d = 0.0;
            if (!m.toDouble(d) || !(d >= 0.0 && d <= 1000000.0)) {
                error = "noise parameters: amplitude_modifiers entry out of range";
                return false;
            }
            p.amplitudeModifiers.push_back(d);
        }
        if (!p.amplitudeModifiers.empty() && static_cast<int>(p.amplitudeModifiers.size()) != p.octaveCount) {
            error = "noise parameters: amplitude_modifiers size must equal octave_count";
            return false;
        }
    }
    if (const JsonValue* nm = json.find("normalize")) {
        if (nm->kind == JsonValue::Kind::Bool) {
            p.normalize = nm->boolean ? NoiseNormalize::Enabled : NoiseNormalize::Disabled;
        } else if (nm->isString() && nm->text == "legacy") {
            p.normalize = NoiseNormalize::Legacy;
        } else {
            error = "noise parameters: normalize must be true, false or \"legacy\"";
            return false;
        }
    }
    out = std::move(p);
    return true;
}

bool loadNoiseParameters(const std::filesystem::path& file, NoiseParameters& out, std::string& error) {
    JsonValue json;
    if (!parseJsonFile(file, json, error)) return false;
    if (!parseNoiseParameters(json, out, error)) {
        error = file.string() + ": " + error;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
namespace {

constexpr double kInputFactor = 1.0181268882175227;     // NormalNoise.INPUT_FACTOR
constexpr double kStandardDeviation = 0.2702247831245211;  // PerlinNoise.STANDARD_DEVIATION
constexpr double kTargetDeviation = 0.3333333333333333;    // NormalNoise.TARGET_DEVIATION

// DoubleStream.sum() (JDK 17+): Kahan summation with the compensation applied at the end.
double compensatedSum(const std::vector<double>& v) {
    double sum = 0.0, comp = 0.0, simple = 0.0;
    for (const double x : v) {
        const double tmp = x - comp;
        const double velvel = sum + tmp;
        comp = (velvel - sum) - tmp;
        sum = velvel;
        simple += x;
    }
    const double r = sum - comp;
    if (std::isnan(r) && std::isinf(simple)) return simple;
    return r;
}

double parityNormalizationFactor(double baseAmplitude, int count, const std::vector<double>& mods) {
    int mn = INT_MAX, mx = INT_MIN;
    for (int i = 0; i < count; ++i) {
        const double m = mods.empty() ? 1.0 : mods[static_cast<std::size_t>(i)];
        if (m != 0.0) {
            mn = i < mn ? i : mn;
            mx = i > mx ? i : mx;
        }
    }
    const double expected = 0.1 * (1.0 + 1.0 / static_cast<double>((mx - mn) + 1));
    return baseAmplitude * 0.5 * kTargetDeviation / expected;
}

}  // namespace

NormalNoise NormalNoise::create(RandomSource& random, const NoiseParameters& p) {
    NormalNoise n;

    // buildOctaves
    double freq = std::ldexp(1.0, p.baseOctave);
    double amp = p.baseAmplitude;
    if (p.normalize != NoiseNormalize::Disabled) {
        amp = amp * (std::ldexp(1.0, p.octaveCount - 1) / (std::ldexp(1.0, p.octaveCount) - 1.0));
    }
    for (int i = 0; i < p.octaveCount; ++i) {
        const double m = p.amplitudeModifiers.empty() ? 1.0 : p.amplitudeModifiers[static_cast<std::size_t>(i)];
        if (m != 0.0) n.octaves_.push_back({p.baseOctave + i, freq, amp * m});
        freq *= 2.0;
        amp *= 0.5;
    }

    // normalization factor
    std::vector<double> absAmps;
    absAmps.reserve(n.octaves_.size());
    for (const OctaveInfo& o : n.octaves_) absAmps.push_back(std::fabs(o.amplitude));
    const double sumAbs = compensatedSum(absAmps);
    double devSq = 0.0;
    for (const OctaveInfo& o : n.octaves_) {
        const double d = kStandardDeviation * std::fabs(o.amplitude);
        devSq += d * d;
    }
    const double dev = std::sqrt(devSq);
    double nf = 0.0;
    if (dev != 0.0) {
        const double devTotal = dev * std::sqrt(2.0);
        const double target = sumAbs * kTargetDeviation;
        nf = target / devTotal;
    }
    if (p.normalize == NoiseNormalize::Legacy && nf != 0.0) {
        const double pf = parityNormalizationFactor(p.baseAmplitude, p.octaveCount, p.amplitudeModifiers);
        nf = pf;
    }
    n.normalizationFactor_ = nf;

    // create(RandomSource)
    const std::unique_ptr<PositionalRandomFactory> fa = random.forkPositional();
    const std::unique_ptr<PositionalRandomFactory> fb = random.forkPositional();
    n.layers_.reserve(n.octaves_.size() * 2);
    for (const OctaveInfo& o : n.octaves_) {
        const std::string seed = "octave_" + std::to_string(o.octave);
        const auto ra = fa->fromHashOf(seed);
        PerlinNoise first(*ra);
        const auto rb = fb->fromHashOf(seed);
        PerlinNoise second(*rb);
        const float a = static_cast<float>(nf * o.amplitude);
        n.layers_.push_back({first, o.frequency, a});
        n.layers_.push_back({second, o.frequency * kInputFactor, a});
    }
    return n;
}

float NormalNoise::get(double x, double y, double z) const {
    float acc = 0.0f;
    for (const Layer& l : layers_) acc = acc + l.amplitude * l.noise.get(x * l.frequency, y * l.frequency, z * l.frequency);
    return acc;
}

float NormalNoise::get(double x, double z) const {
    float acc = 0.0f;
    for (const Layer& l : layers_) acc = acc + l.amplitude * l.noise.get(x * l.frequency, z * l.frequency);
    return acc;
}

// ---------------------------------------------------------------------------------------------------------------
bool parseBlendedNoiseParams(const JsonValue& json, BlendedNoiseParams& out, std::string& error) {
    if (!json.isObject()) {
        error = "old_blended_noise: not an object";
        return false;
    }
    const auto field = [&](const char* key, double lo, double hi, double& dst) {
        const JsonValue* v = json.find(key);
        double d = 0.0;
        if (!v || !v->toDouble(d) || !(d >= lo && d <= hi)) {
            error = std::string("old_blended_noise: ") + key + " missing or out of range";
            return false;
        }
        dst = d;
        return true;
    };
    BlendedNoiseParams p;
    if (!field("xz_scale", 0.001, 1000.0, p.xzScale) || !field("y_scale", 0.001, 1000.0, p.yScale) ||
        !field("xz_factor", 0.001, 1000.0, p.xzFactor) || !field("y_factor", 0.001, 1000.0, p.yFactor) ||
        !field("smear_scale_multiplier", 1.0, 8.0, p.smearScaleMultiplier)) {
        return false;
    }
    out = p;
    return true;
}

BlendedNoise::Stack BlendedNoise::createFbm(RandomSource& random, int firstOctave, double fudgeYScale, double amplitudeParam) {
    Stack st;
    const int n = -firstOctave + 1;
    double freq = 1.0;
    double amp = amplitudeParam / (std::ldexp(1.0, n) - 1.0);
    st.reserve(static_cast<std::size_t>(n));
    for (int i = n - 1; i >= 0; --i) {
        SmearedPerlinNoise noise(random, fudgeYScale * freq);  // consumes the random source here, in this order
        st.push_back({noise, freq, static_cast<float>(amp)});
        freq = freq / 2.0;
        amp = amp * 2.0;
    }
    return st;
}

float BlendedNoise::stackGet(const Stack& s, double x, double y, double z) {
    float acc = 0.0f;
    for (const Layer& l : s) acc = acc + l.amplitude * l.noise.get(x * l.frequency, y * l.frequency, z * l.frequency);
    return acc;
}

BlendedNoise::BlendedNoise(const BlendedNoiseParams& p, RandomSource& random) {
    xzMult_ = 684.412 * p.xzScale;
    yMult_ = 684.412 * p.yScale;
    const double fudge = yMult_ * p.smearScaleMultiplier;
    const double mainFudge = fudge / p.yFactor;
    minLimit_ = createFbm(random, -15, fudge, 0.9999847412109375);
    maxLimit_ = createFbm(random, -15, fudge, 0.9999847412109375);
    main_ = createFbm(random, -7, mainFudge, 12.75);
    xzMainMult_ = xzMult_ / p.xzFactor;
    yMainMult_ = yMult_ / p.yFactor;
}

float BlendedNoise::sample(std::int32_t x, std::int32_t y, std::int32_t z) const {
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double dz = static_cast<double>(z);
    const float m = stackGet(main_, dx * xzMainMult_, dy * yMainMult_, dz * xzMainMult_);
    const float t = clampf(m + 0.5f, 0.0f, 1.0f);
    // The limit noises are pure, so only the ones the blend factor needs are evaluated.
    if (t == 0.0f) return stackGet(minLimit_, dx * xzMult_, dy * yMult_, dz * xzMult_);
    if (t == 1.0f) return stackGet(maxLimit_, dx * xzMult_, dy * yMult_, dz * xzMult_);
    const float lo = stackGet(minLimit_, dx * xzMult_, dy * yMult_, dz * xzMult_);
    const float hi = stackGet(maxLimit_, dx * xzMult_, dy * yMult_, dz * xzMult_);
    return lerpf(t, lo, hi);
}

}  // namespace apex::worldgen
