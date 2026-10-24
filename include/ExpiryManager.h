#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace kv {

// Tracks which keys expire and when. It never touches the data itself; it only
// answers "is this key expired?" and "which keys are expired right now?".
//
// Two structures are kept in sync:
//   deadlines_  key -> absolute deadline (ms)   : O(1) lookup, source of truth
//   heap_       min-heap of (deadline, key)     : O(log n) "what expires next?"
//
// The heap may contain STALE entries (a key whose TTL was changed or removed).
// They are never searched for and deleted; instead, when popped, an entry is
// only honoured if it still matches deadlines_. A periodic compaction keeps the
// number of stale entries bounded.
class ExpiryManager {
public:
    // Returns the current time in milliseconds from any monotonic origin.
    // Injectable so tests can control time.
    using Clock = std::function<std::int64_t()>;
    static Clock systemClock();

    explicit ExpiryManager(Clock clock = systemClock());

    std::int64_t now() const { return clock_(); }

    // Key expires `ttlMs` milliseconds from now (saturating on overflow).
    void setExpiry(const std::string& key, std::int64_t ttlMs);
    // true if the key had an expiry.
    bool removeExpiry(const std::string& key);
    bool hasExpiry(const std::string& key) const;
    // Remaining time in ms (>= 0), or -1 if the key has no expiry.
    std::int64_t ttlMs(const std::string& key) const;
    // Lazy-expiration check. A key is expired once now >= deadline.
    bool isExpired(const std::string& key) const;

    // Active-expiration step: removes and returns keys whose deadline has passed.
    // `maxKeys` bounds the work done in one call (0 = unlimited).
    std::vector<std::string> popExpired(std::size_t maxKeys = 0);

    std::size_t size() const { return deadlines_.size(); }
    std::size_t heapSize() const { return heap_.size(); }  // exposed for tests
    void clear();

private:
    struct Entry {
        std::int64_t when;
        std::string key;
        bool operator>(const Entry& other) const { return when > other.when; }
    };
    using MinHeap = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;

    void compactIfNeeded();

    Clock clock_;
    std::unordered_map<std::string, std::int64_t> deadlines_;
    MinHeap heap_;
};

}  // namespace kv
