#pragma once

//
// test_framework.hpp — Minimal unit test framework (no external deps).
// Provides ASSERT_* and CHECK_* macros with line-number reporting.
//

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace test {

struct TestFailure {
    std::string file;
    int line;
    std::string message;
};

struct TestEntry {
    std::string name;
    void (*fn)();
};

inline std::vector<TestEntry>& registry() {
    static std::vector<TestEntry> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void(*fn)()) {
        registry().push_back({name, fn});
    }
};

inline int g_passed = 0;
inline int g_failed = 0;
inline int g_current_failures_in_test = 0;

#define TEST(name) \
    static void test_##name(); \
    static ::test::Registrar reg_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++::test::g_current_failures_in_test; \
            std::fprintf(stderr, "  [FAIL] %s:%d: CHECK(%s) failed\n", \
                         __FILE__, __LINE__, #cond); \
        } \
    } while(0)

#define CHECK_EQ(a, b) \
    do { \
        auto&& _a = (a); \
        auto&& _b = (b); \
        if (!(_a == _b)) { \
            ++::test::g_current_failures_in_test; \
            std::fprintf(stderr, "  [FAIL] %s:%d: CHECK_EQ(%s, %s) failed: %s != %s\n", \
                         __FILE__, __LINE__, #a, #b, \
                         std::to_string(_a).c_str(), std::to_string(_b).c_str()); \
        } \
    } while(0)

#define CHECK_STR_EQ(a, b) \
    do { \
        std::string _a = (a); \
        std::string _b = (b); \
        if (_a != _b) { \
            ++::test::g_current_failures_in_test; \
            std::fprintf(stderr, "  [FAIL] %s:%d: CHECK_STR_EQ failed:\n    got:      \"%s\"\n    expected: \"%s\"\n", \
                         __FILE__, __LINE__, _a.c_str(), _b.c_str()); \
        } \
    } while(0)

#define CHECK_THROWS(expr) \
    do { \
        bool _threw = false; \
        try { expr; } \
        catch (...) { _threw = true; } \
        if (!_threw) { \
            ++::test::g_current_failures_in_test; \
            std::fprintf(stderr, "  [FAIL] %s:%d: CHECK_THROWS(%s) - no exception thrown\n", \
                         __FILE__, __LINE__, #expr); \
        } \
    } while(0)

#define CHECK_NOTHROWS(expr) \
    do { \
        try { expr; } \
        catch (const std::exception& e) { \
            ++::test::g_current_failures_in_test; \
            std::fprintf(stderr, "  [FAIL] %s:%d: CHECK_NOTHROWS(%s) - unexpected exception: %s\n", \
                         __FILE__, __LINE__, #expr, e.what()); \
        } \
    } while(0)

inline int run_all() {
    auto& tests = registry();
    g_passed = 0;
    g_failed = 0;

    std::printf("Running %zu test(s)...\n\n", tests.size());

    for (const auto& t : tests) {
        g_current_failures_in_test = 0;
        std::printf("  [RUN]  %s\n", t.name.c_str());
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_current_failures_in_test;
            std::fprintf(stderr, "  [FAIL] Unhandled exception: %s\n", e.what());
        } catch (...) {
            ++g_current_failures_in_test;
            std::fprintf(stderr, "  [FAIL] Unknown exception\n");
        }

        if (g_current_failures_in_test == 0) {
            ++g_passed;
            std::printf("  [PASS] %s\n\n", t.name.c_str());
        } else {
            ++g_failed;
            std::printf("  [FAIL] %s (%d assertion(s) failed)\n\n",
                        t.name.c_str(), g_current_failures_in_test);
        }
    }

    std::printf("================================================\n");
    std::printf("Results: %d passed, %d failed (of %d total)\n",
                g_passed, g_failed, g_passed + g_failed);

    return g_failed > 0 ? 1 : 0;
}

} // namespace test

#define TEST_MAIN() \
    int main() { return ::test::run_all(); }
