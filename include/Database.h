#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ExpiryManager.h"
#include "StorageEngine.h"

namespace kv {

// The facade. It owns a StorageEngine and an ExpiryManager and keeps them
// consistent. The command parser talks ONLY to this class.
//
// Invariants this class guarantees:
//   1. Every public operation first runs lazy expiration for the keys it touches,
//      so an expired key is never observable.
//   2. A key that leaves storage (DEL, expiry, SREM of last member, FLUSHALL)
//      also leaves the ExpiryManager, so a stale TTL can never leak onto a new
//      key that happens to reuse the name.
//   3. SET clears any TTL (unless one is given); INCR/DECR keep the TTL.
//
// Not thread-safe; wrap calls in a mutex if you add threads.
class Database {
public:
    explicit Database(ExpiryManager::Clock clock = ExpiryManager::systemClock());

    // ---- strings ----------------------------------------------------------
    void set(const std::string& key, std::string value,
             std::optional<std::int64_t> ttlMs = std::nullopt);
    std::optional<std::string> get(const std::string& key);
    // Adds `delta` to the integer stored at key (missing key counts as 0).
    std::int64_t incrBy(const std::string& key, std::int64_t delta);

    // ---- keys -------------------------------------------------------------
    std::size_t del(const std::vector<std::string>& keys);     // how many existed
    std::size_t exists(const std::vector<std::string>& keys);  // counts duplicates
    // true if the key exists and the timeout was set. seconds/ms <= 0 deletes it.
    bool expire(const std::string& key, std::int64_t seconds);
    bool pexpire(const std::string& key, std::int64_t ms);
    // -2: key missing, -1: no expiry, otherwise remaining time.
    std::int64_t ttl(const std::string& key);   // seconds, rounded to nearest
    std::int64_t pttl(const std::string& key);  // milliseconds
    bool persist(const std::string& key);       // true if a TTL was removed
    std::string type(const std::string& key);   // "none" | "string" | "set"
    std::vector<std::string> keys(const std::string& pattern);
    std::size_t dbsize();
    void flushAll();

    // ---- sets -------------------------------------------------------------
    std::size_t sadd(const std::string& key, const std::vector<std::string>& members);
    std::size_t srem(const std::string& key, const std::vector<std::string>& members);
    std::vector<std::string> smembers(const std::string& key);
    bool sismember(const std::string& key, const std::string& member);
    std::size_t scard(const std::string& key);

    // ---- active expiration --------------------------------------------------
    // Deletes up to `maxKeys` expired keys (0 = all). Returns how many.
    std::size_t activeExpireCycle(std::size_t maxKeys = 0);

private:
    // Lazy expiration. Deletes the key if it has expired; true if it did.
    bool checkExpiry(const std::string& key);
    // Removes the key from storage AND expiry. true if it existed in storage.
    bool removeKey(const std::string& key);

    StorageEngine storage_;
    ExpiryManager expiry_;
};

}  // namespace kv
