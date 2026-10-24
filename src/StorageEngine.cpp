#include "StorageEngine.h"

#include <string>

namespace kv {

namespace {

const StorageEngine::StringSet* asSet(const StorageEngine::Value& v) {
    return std::get_if<StorageEngine::StringSet>(&v);
}

StorageEngine::StringSet* asSet(StorageEngine::Value& v) {
    return std::get_if<StorageEngine::StringSet>(&v);
}

}  // namespace

// ---- strings ----------------------------------------------------------------

void StorageEngine::setString(const std::string& key, std::string value) {
    store_[key] = Value(std::move(value));
}

std::optional<std::string> StorageEngine::getString(const std::string& key) const {
    const auto it = store_.find(key);
    if (it == store_.end()) return std::nullopt;
    if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
    throw WrongTypeError();
}

// ---- generic ----------------------------------------------------------------

bool StorageEngine::del(const std::string& key) { return store_.erase(key) > 0; }

bool StorageEngine::exists(const std::string& key) const { return store_.count(key) > 0; }

StorageEngine::Type StorageEngine::type(const std::string& key) const {
    const auto it = store_.find(key);
    if (it == store_.end()) return Type::None;
    return std::holds_alternative<std::string>(it->second) ? Type::String : Type::Set;
}

std::vector<std::string> StorageEngine::keys(const std::string& pattern) const {
    std::vector<std::string> out;
    const bool matchAll = (pattern == "*");
    for (const auto& kv : store_) {
        if (matchAll || globMatch(pattern, kv.first)) out.push_back(kv.first);
    }
    return out;
}

// ---- sets -------------------------------------------------------------------

std::size_t StorageEngine::sadd(const std::string& key, const std::vector<std::string>& members) {
    auto it = store_.find(key);
    if (it != store_.end() && !asSet(it->second)) throw WrongTypeError();
    if (members.empty()) return 0;  // never create an empty set
    if (it == store_.end()) it = store_.emplace(key, Value(StringSet{})).first;

    StringSet* set = asSet(it->second);
    std::size_t added = 0;
    for (const auto& m : members) {
        if (set->insert(m).second) ++added;
    }
    return added;
}

std::size_t StorageEngine::srem(const std::string& key, const std::vector<std::string>& members) {
    const auto it = store_.find(key);
    if (it == store_.end()) return 0;
    StringSet* set = asSet(it->second);
    if (!set) throw WrongTypeError();

    std::size_t removed = 0;
    for (const auto& m : members) removed += set->erase(m);
    if (set->empty()) store_.erase(it);  // empty sets do not exist
    return removed;
}

std::vector<std::string> StorageEngine::smembers(const std::string& key) const {
    const auto it = store_.find(key);
    if (it == store_.end()) return {};
    const StringSet* set = asSet(it->second);
    if (!set) throw WrongTypeError();
    return std::vector<std::string>(set->begin(), set->end());
}

bool StorageEngine::sismember(const std::string& key, const std::string& member) const {
    const auto it = store_.find(key);
    if (it == store_.end()) return false;
    const StringSet* set = asSet(it->second);
    if (!set) throw WrongTypeError();
    return set->count(member) > 0;
}

std::size_t StorageEngine::scard(const std::string& key) const {
    const auto it = store_.find(key);
    if (it == store_.end()) return 0;
    const StringSet* set = asSet(it->second);
    if (!set) throw WrongTypeError();
    return set->size();
}

// ---- glob -------------------------------------------------------------------

// Iterative matcher with single-star backtracking: O(len(pattern) * len(text))
// worst case, no recursion.
bool StorageEngine::globMatch(const std::string& pattern, const std::string& text) {
    std::size_t p = 0, t = 0;
    std::size_t star = std::string::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

}  // namespace kv
