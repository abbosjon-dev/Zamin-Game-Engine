#pragma once

#include "TestFramework.h"
#include <cmath>

namespace ZaminTest {
    struct Registrar
    {
        Registrar(const char* name, std::function<void()> test, bool slow = false) { Registry().push_back({ name, std::move(test), slow }); }
    };
}
#define TEST(name)                                         \
    static void name();                                    \
    static ZaminTest::Registrar s_reg_##name(#name, name); \
    static void name()
// A test that plays a whole game (tens of seconds): part of every full run and of CI, left out by --quick (the
// quick unit loop; ctest runs it as its own entry, ZaminSliceTests).
#define SLOW_TEST(name)                                          \
    static void name();                                          \
    static ZaminTest::Registrar s_reg_##name(#name, name, true); \
    static void name()
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) ZaminTest::CheckFailed(__FILE__, __LINE__, #condition); \
    } while (false)
#define CHECK_NEAR(a, b, epsilon) CHECK(std::abs((a) - (b)) <= (epsilon))
