#include "Database.h"

#include <limits>

#include "Util.h"

namespace kv {

Database::Database(ExpiryManager::Clock clock) : expiry_(std::move(clock)) {}

// ---- internals ----------------------------------------------------------------

bool Database::checkExpiry(const std::string& key) {
    if (!expiry_.isExpired(key)) return false;
    removeKey(key);
    return true;
}

bool Database::removeKey(const std::string& key) {
    const bool removed = storage_.del(key);
    expiry_.removeExpiry(key);
    return removed;
}

// ---- strings ------------------------------------------------------------------

void Database::set(const std::string& key, std::string value, std::optional<std::int64_t> ttlMs) {
    storage_.setString(key, std::move(value));
    if (ttlMs) {
        expiry_.setExpiry(key, *ttlMs);
    } else {
        expiry_.removeExpiry(key);  // a plain SET discards the old TTL
    }
}

std::optional<std::string> Database::get(const std::string& key) {
    if (checkExpiry(key)) return std::nullopt;
    return storage_.getString(key);
}

std::int64_t Database::incrBy(const std::string& key, std::int64_t delta) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

    checkExpiry(key);
    std::int64_t current = 0;
    if (const auto existing = storage_.getString(key)) {  // throws WrongTypeError
        if (!parseInt64(*existing, current)) throw NotIntegerError();
    }
    if ((delta > 0 && current > kMax - delta) || (delta < 0 && current < kMin - delta)) {
        throw KvError("ERR increment or decrement would overflow");
    }
    current += delta;
    storage_.setString(key, std::to_string(current));  // TTL is left untouched
    return current;
}

// ---- keys ---------------------------------------------------------------------

std::size_t Database::del(const std::vector<std::string>& keys) {
    std::size_t removed = 0;
    for (const auto& key : keys) {
        if (checkExpiry(key)) continue;  // already gone: not counted
        if (removeKey(key)) ++removed;
    }
    return removed;
}

std::size_t Database::exists(const std::vector<std::string>& keys) {
    std::size_t found = 0;
    for (const auto& key : keys) {
        if (checkExpiry(key)) continue;
        if (storage_.exists(key)) ++found;
    }
    return found;
}

bool Database::expire(const std::string& key, std::int64_t seconds) {
    if (seconds <= 0) return pexpire(key, 0);
    return pexpire(key, secondsToMs(seconds));
}

bool Database::pexpire(const std::string& key, std::int64_t ms) {
    if (checkExpiry(key)) return false;
    if (!storage_.exists(key)) return false;
    if (ms <= 0) {
        removeKey(key);  // non-positive timeout deletes the key immediately
        return true;
    }
    expiry_.setExpiry(key, ms);
    return true;
}

std::int64_t Database::pttl(const std::string& key) {
    if (checkExpiry(key)) return -2;
    if (!storage_.exists(key)) return -2;
    return expiry_.ttlMs(key);  // -1 when there is no expiry
}

std::int64_t Database::ttl(const std::string& key) {
    const std::int64_t ms = pttl(key);
    if (ms < 0) return ms;  // -2 / -1 pass through
    return (ms + 500) / 1000;
}

bool Database::persist(const std::string& key) {
    if (checkExpiry(key)) return false;
    if (!storage_.exists(key)) return false;
    return expiry_.removeExpiry(key);
}

std::string Database::type(const std::string& key) {
    if (checkExpiry(key)) return "none";
    switch (storage_.type(key)) {
        case StorageEngine::Type::String: return "string";
        case StorageEngine::Type::Set: return "set";
        default: return "none";
    }
}

std::vector<std::string> Database::keys(const std::string& pattern) {
    std::vector<std::string> out;
    for (auto& key : storage_.keys(pattern)) {  // storage_.keys returns a copy
        if (checkExpiry(key)) continue;
        out.push_back(std::move(key));
    }
    return out;
}

std::size_t Database::dbsize() {
    activeExpireCycle();  // so the answer excludes keys that have already expired
    return storage_.size();
}

void Database::flushAll() {
    storage_.clear();
    expiry_.clear();
}

// ---- sets ---------------------------------------------------------------------

std::size_t Database::sadd(const std::string& key, const std::vector<std::string>& members) {
    checkExpiry(key);
    return storage_.sadd(key, members);
}

std::size_t Database::srem(const std::string& key, const std::vector<std::string>& members) {
    checkExpiry(key);
    const std::size_t removed = storage_.srem(key, members);
    // When the last member goes the key disappears; drop its TTL too (invariant 2).
    if (!storage_.exists(key)) expiry_.removeExpiry(key);
    return removed;
}

std::vector<std::string> Database::smembers(const std::string& key) {
    if (checkExpiry(key)) return {};
    return storage_.smembers(key);
}

bool Database::sismember(const std::string& key, const std::string& member) {
    if (checkExpiry(key)) return false;
    return storage_.sismember(key, member);
}

std::size_t Database::scard(const std::string& key) {
    if (checkExpiry(key)) return 0;
    return storage_.scard(key);
}

// ---- active expiration --------------------------------------------------------

std::size_t Database::activeExpireCycle(std::size_t maxKeys) {
    const std::vector<std::string> expired = expiry_.popExpired(maxKeys);
    for (const auto& key : expired) storage_.del(key);  // expiry entry already removed
    return expired.size();
}

}  // namespace kv
