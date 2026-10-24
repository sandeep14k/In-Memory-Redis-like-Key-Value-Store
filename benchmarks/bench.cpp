// Micro-benchmark: how many operations per second does the store sustain?
//
//   ./bench            # 1,000,000 keys
//   ./bench 200000     # custom key count
//
// Build in Release mode (the CMake default); Debug numbers are meaningless.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "CommandParser.h"
#include "Database.h"

namespace {

using Clock = std::chrono::steady_clock;

template <class F>
void phase(const char* name, std::size_t ops, F&& body) {
    const auto start = Clock::now();
    body();
    const double secs = std::chrono::duration<double>(Clock::now() - start).count();
    std::printf("  %-34s %10zu ops  %8.3f s  %12.0f ops/sec\n", name, ops, secs,
                static_cast<double>(ops) / secs);
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t n = 1000000;
    if (argc > 1) n = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (n == 0) return 1;

    // Build the keys up front so string formatting is not part of the measurement.
    std::vector<std::string> keys, missing;
    keys.reserve(n);
    missing.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        keys.push_back("key:" + std::to_string(i));
        missing.push_back("nokey:" + std::to_string(i));
    }

    std::printf("kvstore benchmark, n = %zu\n\n", n);
    std::uint64_t sink = 0;  // keeps the optimiser from deleting the loops

    {
        std::printf("Database API (no parsing):\n");
        kv::Database db;

        phase("SET", n, [&] {
            for (std::size_t i = 0; i < n; ++i) db.set(keys[i], "value");
        });
        phase("GET (hit)", n, [&] {
            for (std::size_t i = 0; i < n; ++i) sink += db.get(keys[i]).has_value();
        });
        phase("GET (miss)", n, [&] {
            for (std::size_t i = 0; i < n; ++i) sink += db.get(missing[i]).has_value();
        });
        phase("INCR (1,000 hot counters)", n, [&] {
            for (std::size_t i = 0; i < n; ++i) sink += db.incrBy("counter:" + std::to_string(i % 1000), 1);
        });
        phase("SET with TTL (heap push)", n, [&] {
            for (std::size_t i = 0; i < n; ++i) db.set(keys[i], "value", 60000);
        });
        phase("SADD (into one big set)", n, [&] {
            for (std::size_t i = 0; i < n; ++i) db.sadd("bigset", {keys[i]});
        });
        phase("DEL", n, [&] {
            for (std::size_t i = 0; i < n; ++i) sink += db.del({keys[i]});
        });
    }

    {
        std::printf("\nActive expiration:\n");
        kv::Database db;
        for (std::size_t i = 0; i < n; ++i) db.set(keys[i], "value", 1);  // 1 ms TTL
        std::this_thread::sleep_for(std::chrono::milliseconds(20));       // let them all expire
        phase("sweep expired keys (min-heap)", n, [&] { sink += db.activeExpireCycle(); });
        std::printf("  keys left after sweep: %zu\n", db.dbsize());
    }

    {
        const std::size_t m = n / 4 > 0 ? n / 4 : 1;  // parsing is slower; use fewer ops
        std::printf("\nEnd to end through CommandParser (tokenize + dispatch + format):\n");
        kv::Database db;
        kv::CommandParser parser(db);
        std::vector<std::string> setLines, getLines;
        for (std::size_t i = 0; i < m; ++i) {
            setLines.push_back("SET key:" + std::to_string(i) + " value");
            getLines.push_back("GET key:" + std::to_string(i));
        }
        phase("SET via parser", m, [&] {
            for (std::size_t i = 0; i < m; ++i) sink += parser.execute(setLines[i]).size();
        });
        phase("GET via parser", m, [&] {
            for (std::size_t i = 0; i < m; ++i) sink += parser.execute(getLines[i]).size();
        });
    }

    std::printf("\n(checksum %llu)\n", static_cast<unsigned long long>(sink));
    return 0;
}
