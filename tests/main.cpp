// 测试入口：跑完所有登记过的 TEST，全部通过返回 0。
#include <cstdio>
#include <exception>
#include <string>

#include "test_framework.h"

int main() {
    auto& tests = lg_test::registry();
    std::printf("运行 %zu 个测试\n\n", tests.size());

    int passed = 0;
    for (const auto& test : tests) {
        const int before = lg_test::failure_count();
        std::printf("[ RUN  ] %s\n", test.name);
        try {
            test.fn();
        } catch (const std::exception& e) {
            lg_test::report_failure(__FILE__, __LINE__, std::string("抛出异常: ") + e.what());
        } catch (...) {
            lg_test::report_failure(__FILE__, __LINE__, "抛出未知异常");
        }
        if (lg_test::failure_count() == before) {
            ++passed;
            std::printf("[  OK  ] %s\n", test.name);
        } else {
            std::printf("[ FAIL ] %s\n", test.name);
        }
    }

    std::printf("\n%d / %zu 通过\n", passed, tests.size());
    return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
