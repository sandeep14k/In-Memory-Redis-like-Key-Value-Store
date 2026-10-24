#include "CommandParser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>

#include "Util.h"

namespace kv {

namespace {

using Args = std::vector<std::string>;

std::string toUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Quote + escape a value the way redis-cli prints bulk strings.
std::string quote(const std::string& s) {
    std::string out = "\"";
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\x%02x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

std::string fmtOk() { return "OK"; }
std::string fmtNil() { return "(nil)"; }
std::string fmtInt(std::int64_t n) { return "(integer) " + std::to_string(n); }
std::string fmtError(const std::string& msg) { return "(error) " + msg; }
std::string fmtBulk(const std::optional<std::string>& v) { return v ? quote(*v) : fmtNil(); }

std::string fmtArray(const std::vector<std::string>& items) {
    if (items.empty()) return "(empty array)";
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += '\n';
        out += std::to_string(i + 1) + ") " + quote(items[i]);
    }
    return out;
}

std::int64_t parseIntOrThrow(const std::string& s) {
    std::int64_t v = 0;
    if (!parseInt64(s, v)) throw NotIntegerError();
    return v;
}

std::vector<std::string> sortedCopy(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    return v;
}

}  // namespace

// ---- construction -------------------------------------------------------------

CommandParser::CommandParser(Database& db) : db_(db) { registerCommands(); }

void CommandParser::add(const std::string& name, int minArgs, int maxArgs, Handler handler) {
    commands_[name] = CommandSpec{std::move(handler), minArgs, maxArgs};
}

// ---- tokenizer ----------------------------------------------------------------

bool CommandParser::tokenize(const std::string& line, std::vector<std::string>& tokens,
                             std::string& error) {
    static const char* kUnbalanced = "ERR Protocol error: unbalanced quotes in request";
    tokens.clear();
    const std::size_t n = line.size();
    std::size_t i = 0;

    auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };

    while (i < n) {
        while (i < n && isSpace(line[i])) ++i;
        if (i >= n) break;

        std::string token;
        const char first = line[i];

        if (first == '"' || first == '\'') {
            const char q = first;
            bool closed = false;
            ++i;
            while (i < n) {
                const char c = line[i];
                if (q == '"' && c == '\\' && i + 1 < n) {
                    const char e = line[i + 1];
                    switch (e) {
                        case 'n': token += '\n'; break;
                        case 't': token += '\t'; break;
                        case 'r': token += '\r'; break;
                        case '\\': token += '\\'; break;
                        case '"': token += '"'; break;
                        default: token += '\\'; token += e; break;
                    }
                    i += 2;
                } else if (q == '\'' && c == '\\' && i + 1 < n && line[i + 1] == '\'') {
                    token += '\'';
                    i += 2;
                } else if (c == q) {
                    closed = true;
                    ++i;
                    break;
                } else {
                    token += c;
                    ++i;
                }
            }
            // A closing quote must be followed by whitespace or end of line.
            if (!closed || (i < n && !isSpace(line[i]))) {
                error = kUnbalanced;
                return false;
            }
        } else {
            while (i < n && !isSpace(line[i])) token += line[i++];
        }
        tokens.push_back(std::move(token));
    }
    return true;
}

// ---- execution ----------------------------------------------------------------

std::string CommandParser::execute(const std::string& line) {
    std::vector<std::string> tokens;
    std::string error;
    if (!tokenize(line, tokens, error)) return fmtError(error);
    if (tokens.empty()) return "";

    const std::string name = toUpper(tokens[0]);
    const auto it = commands_.find(name);
    if (it == commands_.end()) {
        return fmtError("ERR unknown command '" + tokens[0] + "'");
    }

    const Args args(tokens.begin() + 1, tokens.end());
    const CommandSpec& spec = it->second;
    const int n = static_cast<int>(args.size());
    if (n < spec.minArgs || (spec.maxArgs >= 0 && n > spec.maxArgs)) {
        return fmtError("ERR wrong number of arguments for '" + toLower(name) + "' command");
    }

    try {
        return spec.handler(args);
    } catch (const KvError& e) {
        return fmtError(e.what());
    }
}

// ---- command table ------------------------------------------------------------

void CommandParser::registerCommands() {
    // --- connection / server ---
    add("PING", 0, 1, [](const Args& a) {
        return a.empty() ? std::string("PONG") : quote(a[0]);
    });

    add("HELP", 0, 0, [this](const Args&) {
        std::vector<std::string> names;
        for (const auto& kv : commands_) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        std::string out = "Commands:";
        for (const auto& nm : names) out += " " + nm;
        out += "\nType EXIT or QUIT to leave.";
        return out;
    });

    add("DBSIZE", 0, 0, [this](const Args&) {
        return fmtInt(static_cast<std::int64_t>(db_.dbsize()));
    });

    add("FLUSHALL", 0, 0, [this](const Args&) {
        db_.flushAll();
        return fmtOk();
    });

    // --- strings ---
    // SET key value [EX seconds | PX milliseconds]
    add("SET", 2, 4, [this](const Args& a) {
        std::optional<std::int64_t> ttlMs;
        if (a.size() == 3) throw KvError("ERR syntax error");
        if (a.size() == 4) {
            const std::string opt = toUpper(a[2]);
            if (opt != "EX" && opt != "PX") throw KvError("ERR syntax error");
            const std::int64_t amount = parseIntOrThrow(a[3]);
            if (amount <= 0) throw KvError("ERR invalid expire time in 'set' command");
            ttlMs = (opt == "EX") ? secondsToMs(amount) : amount;
        }
        db_.set(a[0], a[1], ttlMs);
        return fmtOk();
    });

    add("GET", 1, 1, [this](const Args& a) { return fmtBulk(db_.get(a[0])); });

    add("INCR", 1, 1, [this](const Args& a) { return fmtInt(db_.incrBy(a[0], 1)); });
    add("DECR", 1, 1, [this](const Args& a) { return fmtInt(db_.incrBy(a[0], -1)); });

    add("INCRBY", 2, 2, [this](const Args& a) {
        return fmtInt(db_.incrBy(a[0], parseIntOrThrow(a[1])));
    });

    add("DECRBY", 2, 2, [this](const Args& a) {
        const std::int64_t n = parseIntOrThrow(a[1]);
        if (n == std::numeric_limits<std::int64_t>::min()) {
            throw KvError("ERR decrement would overflow");
        }
        return fmtInt(db_.incrBy(a[0], -n));
    });

    // --- keys ---
    add("DEL", 1, -1, [this](const Args& a) {
        return fmtInt(static_cast<std::int64_t>(db_.del(a)));
    });

    add("EXISTS", 1, -1, [this](const Args& a) {
        return fmtInt(static_cast<std::int64_t>(db_.exists(a)));
    });

    add("EXPIRE", 2, 2, [this](const Args& a) {
        return fmtInt(db_.expire(a[0], parseIntOrThrow(a[1])) ? 1 : 0);
    });

    add("PEXPIRE", 2, 2, [this](const Args& a) {
        return fmtInt(db_.pexpire(a[0], parseIntOrThrow(a[1])) ? 1 : 0);
    });

    add("TTL", 1, 1, [this](const Args& a) { return fmtInt(db_.ttl(a[0])); });
    add("PTTL", 1, 1, [this](const Args& a) { return fmtInt(db_.pttl(a[0])); });

    add("PERSIST", 1, 1, [this](const Args& a) { return fmtInt(db_.persist(a[0]) ? 1 : 0); });

    add("TYPE", 1, 1, [this](const Args& a) { return db_.type(a[0]); });

    add("KEYS", 1, 1, [this](const Args& a) {
        return fmtArray(sortedCopy(db_.keys(a[0])));
    });

    // --- sets ---
    add("SADD", 2, -1, [this](const Args& a) {
        const Args members(a.begin() + 1, a.end());
        return fmtInt(static_cast<std::int64_t>(db_.sadd(a[0], members)));
    });

    add("SREM", 2, -1, [this](const Args& a) {
        const Args members(a.begin() + 1, a.end());
        return fmtInt(static_cast<std::int64_t>(db_.srem(a[0], members)));
    });

    add("SMEMBERS", 1, 1, [this](const Args& a) {
        return fmtArray(sortedCopy(db_.smembers(a[0])));
    });

    add("SISMEMBER", 2, 2, [this](const Args& a) {
        return fmtInt(db_.sismember(a[0], a[1]) ? 1 : 0);
    });

    add("SCARD", 1, 1, [this](const Args& a) {
        return fmtInt(static_cast<std::int64_t>(db_.scard(a[0])));
    });
}

}  // namespace kv
