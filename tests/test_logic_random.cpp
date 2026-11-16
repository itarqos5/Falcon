// The legacy random source of the Nether and the End (worldgen/random.hpp).
#include "falcon/worldgen/random.hpp"
#include "test_logic.hpp"

// The legacy random source of the Nether and the End is java.util.Random: the numbers Java gives for a few seeds (new Random(0).nextInt() is -1155484576, new Random(42).nextInt()
// is -1170105035, new Random(0).nextDouble() is 0.730967787376657), and the either-kind factory draws what the plain classes draw.
void testLegacyRandom() {
    using namespace falcon::worldgen;
    LCHECK(LegacyRandomSource(0).nextInt() == -1155484576);
    LCHECK(LegacyRandomSource(42).nextInt() == -1170105035);
    LCHECK(LegacyRandomSource(0).nextDouble() == 0.730967787376657);
    const std::int64_t seed = 0x1234567890ABCDEFll;
    const AnyPositionalFactory legacy(seed);
    LCHECK(legacy.isLegacy());
    AnyRandom a = legacy.makeAt(3, -60, 7);
    LegacyRandomSource b(mthGetSeed(3, -60, 7) ^ seed);
    for (int i = 0; i < 4; ++i) LCHECK(a.nextDouble() == b.nextDouble());
    AnyRandom named = legacy.makeFromHashOf("minecraft:bedrock_floor");
    LegacyRandomSource namedDirect(static_cast<std::int64_t>(javaStringHashCode("minecraft:bedrock_floor")) ^ seed);
    LCHECK(named.nextFloat() == namedDirect.nextFloat());
    const AnyPositionalFactory xoro{XoroshiroPositionalRandomFactory(1, 2)};
    LCHECK(!xoro.isLegacy());
    AnyRandom x = xoro.makeAt(1, 2, 3);
    XoroshiroRandomSource xDirect = XoroshiroPositionalRandomFactory(1, 2).makeAt(1, 2, 3);
    LCHECK(x.nextInt(100) == xDirect.nextInt(100));
}
