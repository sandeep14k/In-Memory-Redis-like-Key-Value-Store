#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "Errors.h"

namespace kv {

// The data layer. It maps keys to values (a string or a set of strings) and
// knows NOTHING about time, TTLs or commands. All operations are O(1) average
// unless noted otherwise.
class StorageEngine {
public:
    using StringSet = std::unordered_set<std::string>;
    using Value = std::variant<std::string, StringSet>;

    enum class Type { None, String, Set };

    // ---- strings ----------------------------------------------------------
    // Overwrites whatever is stored under `key`, whatever its type.
    void setString(const std::string& key, std::string value);
    // nullopt if the key is missing. Throws WrongTypeError if it holds a set.
    std::optional<std::string> getString(const std::string& key) const;

    // ---- generic ----------------------------------------------------------
    bool del(const std::string& key);  // true if a key was removed
    bool exists(const std::string& key) const;
    Type type(const std::string& key) const;
    std::size_t size() const { return store_.size(); }
    void clear() { store_.clear(); }
    // All keys matching a glob pattern ('*' and '?'). O(n). Unordered.
    std::vector<std::string> keys(const std::string& pattern = "*") const;

    // ---- sets -------------------------------------------------------------
    // Returns how many members were newly added. Throws WrongTypeError if the
    // key holds a string.
    std::size_t sadd(const std::string& key, const std::vector<std::string>& members);
    // Returns how many members were removed. The key disappears when its set
    // becomes empty (same as Redis).
    std::size_t srem(const std::string& key, const std::vector<std::string>& members);
    std::vector<std::string> smembers(const std::string& key) const;  // O(n)
    bool sismember(const std::string& key, const std::string& member) const;
    std::size_t scard(const std::string& key) const;

    // Glob matcher supporting '*' (any run) and '?' (any single char).
    static bool globMatch(const std::string& pattern, const std::string& text);

private:
    std::unordered_map<std::string, Value> store_;
};

}  // namespace kv
