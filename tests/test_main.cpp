#include "framework.h"

int main() {
    int total = 0;
    int failed = 0;

    for (auto &tc : test_registry()) {
        current_test_failures() = 0;
        std::printf("RUN  %s\n", tc.name);
        tc.fn();
        total++;
        if (current_test_failures() > 0) {
            failed++;
            std::printf("FAIL %s\n", tc.name);
        } else {
            std::printf("PASS %s\n", tc.name);
        }
    }

    std::printf("\n%d/%d tests passed\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}
