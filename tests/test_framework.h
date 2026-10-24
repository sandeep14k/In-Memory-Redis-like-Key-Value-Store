#pragma once

// A tiny dependency-free test harness (so the project builds with nothing but a
// C++17 compiler). Swap in GoogleTest/Catch2 later if you like; the TEST /
// CHECK_* names deliberately mirror them.

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace tf {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back(TestCase{name, std::move(fn)});
    }
};

// ---- helpers for readable test code ----------------------------------------
inline std::vector<std::string> V(std::initializer_list<std::string> items) {
    return std::vector<std::string>(items);
}

inline std::vector<std::string> sorted(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// ---- printing ---------------------------------------------------------------
template <class T>
std::string show(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

inline std::string show(const std::optional<std::string>& v) {
    return v ? "\"" + *v + "\"" : std::string("(nullopt)");
}

inline std::string show(const std::vector<std::string>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", \"" : "\"") + v[i] + "\"";
    return out + "]";
}

// ---- comparison (integers of different signedness compare by value) ---------
template <class A, class B>
bool valuesEqual(const A& a, const B& b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        return static_cast<long long>(a) == static_cast<long long>(b);
    } else {
        return a == b;
    }
}

template <class A, class B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    if (!valuesEqual(a, b)) {
        ++failures();
        std::cerr << file << ":" << line << ": CHECK_EQ(" << ea << ", " << eb << ") failed\n"
                  << "    left : " << show(a) << "\n"
                  << "    right: " << show(b) << "\n";
    }
}

}  // namespace tf

#define TEST(name)                                          \
    static void name();                                     \
    static tf::Registrar registrar_##name(#name, name);     \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ++tf::failures();                                                         \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n"; \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b) tf::checkEq((a), (b), #a, #b, __FILE__, __LINE__)

#define CHECK_THROWS(expr, ExType)                                                      \
    do {                                                                                \
        bool thrown_ = false;                                                           \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const ExType&) {                                                       \
            thrown_ = true;                                                             \
        } catch (...) {                                                                 \
        }                                                                               \
        if (!thrown_) {                                                                 \
            ++tf::failures();                                                           \
            std::cerr << __FILE__ << ":" << __LINE__ << ": expected " #ExType " from " #expr "\n"; \
        }                                                                               \
    } while (0)
