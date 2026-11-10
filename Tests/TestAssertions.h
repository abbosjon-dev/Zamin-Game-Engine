#pragma once

#include "TestFramework.h"
#include <cmath>

namespace ZaminTest {
    struct Registrar
    {
        Registrar(const char* name, std::function<void()> test) { Registry().push_back({ name, std::move(test) }); }
    };
}
#define TEST(name)                                         \
    static void name();                                    \
    static ZaminTest::Registrar s_reg_##name(#name, name); \
    static void name()
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) ZaminTest::CheckFailed(__FILE__, __LINE__, #condition); \
    } while (false)
#define CHECK_NEAR(a, b, epsilon) CHECK(std::abs((a) - (b)) <= (epsilon))
