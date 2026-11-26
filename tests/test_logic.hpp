#pragma once

// The tests of the game's rules that need no game data and no graphics card: the clock, the mobs' brains, the portals. Each file has one function that test_logic.cpp calls.
#include <cstdio>

extern int g_logicFailures;

#define LCHECK(expr)                                                                      \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_logicFailures;                                                            \
        }                                                                                 \
    } while (false)

void testDayCycle();
void testMobPath();
void testMobMovement();
void testMobFloats();
void testEndSpikes();
void testLegacyRandom();
