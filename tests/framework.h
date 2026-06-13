#pragma once

// Tiny, dependency-free unit test framework for the DALI protocol core.
//
// Usage:
//   TEST(some_name) {
//       CHECK(condition);
//       CHECK_EQ(actual, expected);
//   }
//
// All TEST() blocks across all translation units self-register; test_main.cpp
// runs them all and prints a summary.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
    const char *name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &test_registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct TestRegistrar {
    TestRegistrar(const char *name, std::function<void()> fn) {
        test_registry().push_back({name, std::move(fn)});
    }
};

// Number of CHECK failures in the currently-running test.
inline int &current_test_failures() {
    static int n = 0;
    return n;
}

#define TEST(name)                                                            \
    static void test_##name();                                                \
    static TestRegistrar registrar_##name(#name, test_##name);                \
    static void test_##name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "    FAIL %s:%d: CHECK(%s)\n", __FILE__,      \
                          __LINE__, #cond);                                    \
            current_test_failures()++;                                         \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        auto _a = (a);                                                          \
        auto _b = (b);                                                          \
        if (!(_a == _b)) {                                                      \
            std::fprintf(stderr,                                                \
                          "    FAIL %s:%d: CHECK_EQ(%s, %s): %lld != %lld\n",    \
                          __FILE__, __LINE__, #a, #b, (long long)_a,             \
                          (long long)_b);                                       \
            current_test_failures()++;                                         \
        }                                                                       \
    } while (0)

#define CHECK_HEX_EQ(a, b)                                                      \
    do {                                                                        \
        auto _a = (a);                                                         \
        auto _b = (b);                                                         \
        if (!(_a == _b)) {                                                     \
            std::fprintf(stderr,                                               \
                          "    FAIL %s:%d: CHECK_HEX_EQ(%s, %s): 0x%llx != 0x%llx\n", \
                          __FILE__, __LINE__, #a, #b,                          \
                          (unsigned long long)_a, (unsigned long long)_b);     \
            current_test_failures()++;                                         \
        }                                                                       \
    } while (0)
