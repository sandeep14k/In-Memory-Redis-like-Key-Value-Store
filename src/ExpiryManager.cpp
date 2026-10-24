#include "ExpiryManager.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace kv {

ExpiryManager::Clock ExpiryManager::systemClock() {
    return [] {
        using namespace std::chrono;
        return static_cast<std::int64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    };
}

ExpiryManager::ExpiryManager(Clock clock) : clock_(std::move(clock)) {}

void ExpiryManager::setExpiry(const std::string& key, std::int64_t ttlMs) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    const std::int64_t t = now();
    const std::int64_t when = (ttlMs > kMax - t) ? kMax : t + ttlMs;

    deadlines_[key] = when;  // overwrites any previous deadline
    heap_.push(Entry{when, key});
    compactIfNeeded();
}

bool ExpiryManager::removeExpiry(const std::string& key) {
    const bool had = deadlines_.erase(key) > 0;
    if (had) compactIfNeeded();  // its heap entry is now stale
    return had;
}

bool ExpiryManager::hasExpiry(const std::string& key) const {
    return deadlines_.count(key) > 0;
}

std::int64_t ExpiryManager::ttlMs(const std::string& key) const {
    const auto it = deadlines_.find(key);
    if (it == deadlines_.end()) return -1;
    return std::max<std::int64_t>(0, it->second - now());
}

bool ExpiryManager::isExpired(const std::string& key) const {
    const auto it = deadlines_.find(key);
    return it != deadlines_.end() && now() >= it->second;
}

std::vector<std::string> ExpiryManager::popExpired(std::size_t maxKeys) {
    std::vector<std::string> expired;
    const std::int64_t t = now();

    while (!heap_.empty() && heap_.top().when <= t) {
        if (maxKeys != 0 && expired.size() >= maxKeys) break;

        Entry top = heap_.top();
        heap_.pop();

        // Honour the entry only if it still describes the key's current deadline.
        const auto it = deadlines_.find(top.key);
        if (it != deadlines_.end() && it->second == top.when) {
            deadlines_.erase(it);
            expired.push_back(std::move(top.key));
        }
        // else: stale entry (TTL changed or removed) -> silently dropped.
    }
    return expired;
}

void ExpiryManager::clear() {
    deadlines_.clear();
    heap_ = MinHeap();
}

// Stale entries are only discarded when they reach the top of the heap, so a
// key whose TTL is rewritten again and again would grow the heap without bound.
// When the heap is much larger than the live set, rebuild it (O(n)).
void ExpiryManager::compactIfNeeded() {
    if (heap_.size() <= 2 * deadlines_.size() + 64) return;

    std::vector<Entry> live;
    live.reserve(deadlines_.size());
    for (const auto& kv : deadlines_) live.push_back(Entry{kv.second, kv.first});
    heap_ = MinHeap(std::greater<Entry>(), std::move(live));
}

}  // namespace kv
