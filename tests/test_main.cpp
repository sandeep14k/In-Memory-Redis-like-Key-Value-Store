#include <exception>
#include <iostream>

#include "test_framework.h"

int main() {
    int failedTests = 0;
    for (const auto& t : tf::registry()) {
        const int before = tf::failures();
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++tf::failures();
            std::cerr << t.name << ": unexpected exception: " << e.what() << "\n";
        } catch (...) {
            ++tf::failures();
            std::cerr << t.name << ": unexpected unknown exception\n";
        }
        const bool ok = (tf::failures() == before);
        if (!ok) ++failedTests;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << t.name << "\n";
    }
    const std::size_t total = tf::registry().size();
    std::cout << "\n" << (total - failedTests) << "/" << total << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
