#include "test_logic.hpp"

int g_logicFailures = 0;

int main() {
    std::printf("logic tests\n");
    testDayCycle();
    testMobPath();
    testLegacyRandom();
    if (g_logicFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_logicFailures);
        return 1;
    }
    std::printf("all logic tests passed\n");
    return 0;
}
