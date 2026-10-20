// 极简测试框架（约 80 行）。
//
// 为什么不用 GoogleTest：本机离线（HTTPS 到 GitHub/PyPI 都不通），FetchContent 拉不到。
// 等有了网络或上了 CI，把 tests/ 换成 gtest 只是改 CMakeLists —— 测试代码写法几乎一致。
//
// 用法：
//   TEST(MyCase) { CHECK(1 + 1 == 2); CHECK_EQ(add(2, 3), 5); }
#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace lg_test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back(TestCase{name, fn}); }
};

inline void report_failure(const char* file, int line, const std::string& message) {
    ++failure_count();
    std::printf("    [FAIL] %s:%d  %s\n", file, line, message.c_str());
}

inline std::string to_debug(int value) { return std::to_string(value); }
inline std::string to_debug(long long value) { return std::to_string(value); }
inline std::string to_debug(unsigned long long value) { return std::to_string(value); }
inline std::string to_debug(bool value) { return value ? "true" : "false"; }
inline std::string to_debug(const char* value) { return value != nullptr ? value : "(null)"; }
inline std::string to_debug(const std::string& value) { return value; }
inline std::string to_debug(std::string_view value) { return std::string(value); }

}  // namespace lg_test

#define TEST(name)                                                 \
    static void name();                                            \
    static ::lg_test::Registrar lg_registrar_##name(#name, &name);  \
    static void name()

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ::lg_test::report_failure(__FILE__, __LINE__,                                  \
                                      std::string("CHECK 失败: ") + #cond);                \
        }                                                                                  \
    } while (false)

#define CHECK_EQ(actual, expected)                                                         \
    do {                                                                                   \
        const auto lg_actual_ = (actual);                                                  \
        const auto lg_expected_ = (expected);                                              \
        if (!(lg_actual_ == lg_expected_)) {                                               \
            ::lg_test::report_failure(                                                     \
                __FILE__, __LINE__,                                                        \
                std::string("CHECK_EQ 失败: ") + #actual + " == " + #expected + "（实际 " + \
                    ::lg_test::to_debug(lg_actual_) + "，期望 " +                          \
                    ::lg_test::to_debug(lg_expected_) + "）");                             \
        }                                                                                  \
    } while (false)
