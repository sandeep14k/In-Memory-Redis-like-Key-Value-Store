#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

namespace kv {

// Strict base-10 parse, modelled on Redis: optional leading '-', digits only,
// no whitespace, no '+', no leading zeros, and the value must fit in int64.
inline bool parseInt64(const std::string& s, std::int64_t& out) {
    if (s.empty()) return false;
    const std::size_t start = (s[0] == '-') ? 1 : 0;
    if (start == s.size()) return false;                        // just "-"
    if (s[start] == '0' && s.size() > start + 1) return false;  // leading zero
    if (s[start] == '0' && start == 1) return false;            // "-0"
    for (std::size_t i = start; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    std::int64_t value = 0;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    const auto res = std::from_chars(first, last, value);
    if (res.ec != std::errc() || res.ptr != last) return false;  // out of range
    out = value;
    return true;
}

// seconds -> milliseconds, saturating instead of overflowing.
inline std::int64_t secondsToMs(std::int64_t seconds) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    if (seconds > kMax / 1000) return kMax;
    if (seconds < kMin / 1000) return kMin;
    return seconds * 1000;
}

}  // namespace kv
