// Interactive REPL for the in-memory key-value store.
//
//   $ ./kvstore
//   > SET name Rahul
//   OK
//   > GET name
//   "Rahul"
//
// Commands can also be piped in:   printf 'SET a 1\nGET a\n' | ./kvstore

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

#include "CommandParser.h"
#include "Database.h"

#ifdef _WIN32
#include <io.h>
static bool stdinIsTerminal() { return _isatty(_fileno(stdin)) != 0; }
#else
#include <unistd.h>
static bool stdinIsTerminal() { return isatty(STDIN_FILENO) != 0; }
#endif

namespace {

std::string trimmedUpper(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string out = s.substr(b, e - b);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

int main() {
    kv::Database db;
    kv::CommandParser parser(db);

    const bool interactive = stdinIsTerminal();
    if (interactive) {
        std::cout << "kvstore - in-memory key-value store. Type HELP for commands, EXIT to quit.\n";
    }

    std::string line;
    while (true) {
        if (interactive) std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;  // EOF (Ctrl-D or end of piped input)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const std::string upper = trimmedUpper(line);
        if (upper == "EXIT" || upper == "QUIT") break;

        // Active expiration: reclaim a bounded batch of expired keys between commands.
        db.activeExpireCycle(100);

        const std::string reply = parser.execute(line);
        if (!reply.empty()) std::cout << reply << '\n';
    }
    return 0;
}
