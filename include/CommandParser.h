#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Database.h"

namespace kv {

// Turns a line of text such as   SET name "Rahul Kumar" EX 60   into a call on
// the Database and formats the result the way redis-cli does.
//
//   1. tokenize  : split on whitespace, honouring "double" and 'single' quotes
//   2. normalize : command name is case-insensitive
//   3. validate  : argument count is checked against the command's arity
//   4. dispatch  : table lookup (no if/else chain)
//   5. format    : OK, (integer) n, "bulk", (nil), 1) "a" 2) "b", (error) ...
class CommandParser {
public:
    explicit CommandParser(Database& db);
    CommandParser(const CommandParser&) = delete;
    CommandParser& operator=(const CommandParser&) = delete;

    // Executes one command line and returns the formatted reply
    // (empty string for a blank line).
    std::string execute(const std::string& line);

    // Splits a line into tokens. Returns false and fills `error` when quotes are
    // unbalanced. Public so it can be unit-tested.
    static bool tokenize(const std::string& line, std::vector<std::string>& tokens,
                         std::string& error);

private:
    using Args = std::vector<std::string>;  // arguments AFTER the command name
    using Handler = std::function<std::string(const Args&)>;

    struct CommandSpec {
        Handler handler;
        int minArgs;
        int maxArgs;  // -1 = unlimited
    };

    void add(const std::string& name, int minArgs, int maxArgs, Handler handler);
    void registerCommands();

    Database& db_;
    std::unordered_map<std::string, CommandSpec> commands_;
};

}  // namespace kv
