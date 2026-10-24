# kvstore: an In-Memory Redis-like Key-Value Store (C++17)

A single-process, in-memory key-value store with a Redis-style command interface,
key expiration (TTL), and string and set values. It is built from four small,
separately testable components and has **no external dependencies**.

```
> SET name Rahul
OK
> SET session abc123 EX 30
OK
> TTL session
(integer) 30
> SADD fruits apple banana apple
(integer) 2
> SMEMBERS fruits
1) "apple"
2) "banana"
```

## Highlights

- **O(1) average** reads and writes, from `std::unordered_map` (keys) and `std::unordered_set` (set values).
- **Four components with single responsibilities:** Command Parser, Database, Storage Engine, Expiry Manager.
- **Hybrid expiration like Redis:** *lazy* (check on access) plus *active* (min-heap sweep).
- **Redis-compatible semantics:** `WRONGTYPE` errors, `TTL` returning `-1`/`-2`, `SET` clearing TTLs, `INCR` keeping them, and so on.
- **Deterministic tests:** time is injected, so TTL tests run instantly with no `sleep()`.
- 75 unit tests, clean under `-Wall -Wextra -Wpedantic`, AddressSanitizer and UBSan.

## Architecture

```
            "SET name Rahul EX 60"
                     |
                     v
        +-------------------------+
        |     CommandParser       |  tokenize -> validate arity -> dispatch -> format reply
        +------------+------------+
                     |  typed C++ calls (set, get, expire, ...)
                     v
        +-------------------------+
        |        Database         |  facade: keeps storage and expiry consistent,
        |                         |  runs lazy expiration before every access
        +------+-------------+----+
               |             |
               v             v
   +----------------+   +------------------+
   | StorageEngine  |   |  ExpiryManager   |
   | key -> Value   |   | key -> deadline  |
   | (string | set) |   | + min-heap       |
   +----------------+   +------------------+
   knows nothing        knows nothing about
   about time           the data itself
```

| Component | Responsibility | Knows about |
|---|---|---|
| `CommandParser` | Text in, text out. Tokenizing, quoting, arity checks, reply formatting. | `Database` only |
| `Database` | Combines storage and expiry. Enforces Redis semantics and invariants. | `StorageEngine`, `ExpiryManager` |
| `StorageEngine` | `unordered_map<string, variant<string, unordered_set<string>>>`. Pure data. | nothing |
| `ExpiryManager` | Deadlines and the min-heap. Pure time. | nothing (clock is injected) |

## Project layout

```
kvstore/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── Errors.h            # KvError, WrongTypeError, NotIntegerError
│   ├── Util.h              # strict int64 parsing, saturating seconds->ms
│   ├── StorageEngine.h
│   ├── ExpiryManager.h
│   ├── Database.h
│   └── CommandParser.h
├── src/
│   ├── StorageEngine.cpp
│   ├── ExpiryManager.cpp
│   ├── Database.cpp
│   ├── CommandParser.cpp
│   └── main.cpp            # the REPL
├── tests/
│   ├── test_framework.h    # tiny dependency-free test harness
│   ├── fake_clock.h        # controllable time for TTL tests
│   ├── test_main.cpp
│   ├── test_storage.cpp
│   ├── test_expiry.cpp
│   ├── test_database.cpp
│   └── test_parser.cpp
└── benchmarks/
    └── bench.cpp
```

## Build and run

Requirements: a C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+) and CMake 3.14+.

```bash
cmake -S . -B build            # defaults to a Release build
cmake --build build -j

./build/kvstore                # start the REPL (type EXIT to quit)
./build/kv_tests               # run the unit tests (or: cd build && ctest)
./build/bench                  # run the benchmark (optional arg: key count)
```

Without CMake:

```bash
g++ -std=c++17 -O2 -Iinclude src/*.cpp -o kvstore
```

Commands can also be piped in:

```bash
printf 'SET a 1\nINCR a\nGET a\n' | ./build/kvstore
```

## Supported commands

Command names are case-insensitive. Replies mimic `redis-cli`.

| Command | Description | Reply |
|---|---|---|
| `PING [msg]` | Liveness check | `PONG` / `"msg"` |
| `SET key value [EX sec \| PX ms]` | Set a string (overwrites any type, clears old TTL) | `OK` |
| `GET key` | Get a string | `"value"` / `(nil)` |
| `DEL key [key ...]` | Delete keys | number deleted |
| `EXISTS key [key ...]` | Count existing keys (duplicates counted) | integer |
| `EXPIRE key seconds` | Set a TTL (`<= 0` deletes the key) | `1` set, `0` no such key |
| `PEXPIRE key ms` | Same, in milliseconds | `1` / `0` |
| `TTL key` | Remaining seconds | `-2` missing, `-1` no TTL |
| `PTTL key` | Remaining milliseconds | `-2` / `-1` / ms |
| `PERSIST key` | Remove a TTL | `1` removed, `0` otherwise |
| `INCR` / `DECR key` | Atomic integer add / subtract 1 | new value |
| `INCRBY` / `DECRBY key n` | Add / subtract `n` | new value |
| `SADD key m [m ...]` | Add set members | number newly added |
| `SREM key m [m ...]` | Remove set members (empty set deletes the key) | number removed |
| `SMEMBERS key` | All members (sorted for display) | array |
| `SISMEMBER key m` | Membership test | `1` / `0` |
| `SCARD key` | Set size | integer |
| `KEYS pattern` | Keys matching a glob (`*`, `?`), sorted | array |
| `TYPE key` | `string`, `set` or `none` | simple string |
| `DBSIZE` | Number of live keys | integer |
| `FLUSHALL` | Delete everything | `OK` |
| `HELP` | List commands | text |
| `EXIT` / `QUIT` | Leave the REPL | n/a |

Values with spaces need quotes: `SET msg "hello world"`. Double quotes support
`\"`, `\\`, `\n`, `\t`, `\r`; single quotes support `\'`.

Error replies follow Redis wording:

```
> SADD s a
(integer) 1
> GET s
(error) WRONGTYPE Operation against a key holding the wrong kind of value
> SET n abc
OK
> INCR n
(error) ERR value is not an integer or out of range
> GET
(error) ERR wrong number of arguments for 'get' command
```

## Design notes

### 1. Storage Engine

```cpp
using Value = std::variant<std::string, std::unordered_set<std::string>>;
std::unordered_map<std::string, Value> store_;
```

A `std::variant` gives type-safe values without inheritance or `void*`. Accessing a
value as the wrong alternative throws `WrongTypeError`, which the parser turns into
Redis's `WRONGTYPE` reply. Adding a list or hash type later is a new variant
alternative plus new methods.

### 2. Expiration: lazy + active

Redis uses both strategies, and so does this project.

**Lazy expiration** (correctness). Every `Database` operation first calls
`checkExpiry(key)`. If the key's deadline has passed, it is deleted on the spot and
treated as missing. An expired key is therefore *never observable*, even if no sweep
has run.

**Active expiration** (memory). Lazy expiration alone leaks memory for keys that are
never touched again. `ExpiryManager` keeps a min-heap of `(deadline, key)`, and
`Database::activeExpireCycle()` pops entries from the top while their deadline has
passed. The REPL runs a bounded sweep (100 keys) between commands. The cost is
O(k log n) for k expired keys, with no scan of unexpired keys.

**Stale heap entries.** `std::priority_queue` cannot delete from the middle, so
changing or removing a TTL leaves an old entry behind. Two rules keep this correct
and bounded:

1. *On pop, verify.* An entry is honoured only if it still equals the key's current
   deadline in the `deadlines_` map. Otherwise it is dropped.
2. *Compact when bloated.* If the heap grows beyond `2 x live keys + 64`, it is
   rebuilt from the map in O(n). A key whose TTL is rewritten a million times cannot
   grow memory without bound (see `Expiry_HeapStaysBoundedWhen...` tests).

A key is expired when `now >= deadline`.

### 3. Database invariants

1. **Lazy check first.** Every operation expires the keys it touches before using them.
2. **Storage and expiry never disagree.** Every path that removes a key
   (`DEL`, expiry, `SREM` of the last member, `FLUSHALL`) also clears its TTL, so a
   stale deadline can never attach itself to a new key reusing the name.
3. **Redis TTL rules.** `SET` clears the TTL unless one is supplied; `INCR`/`DECR`
   keep it; `EXPIRE` with a non-positive value deletes the key.

### 4. Command parser

- **Table-driven dispatch.** `unordered_map<string, {handler, minArgs, maxArgs}>`:
  adding a command is one `add(...)` call, and arity validation is shared.
- **Tokenizer** handles whitespace, quotes and escapes, and reports unbalanced quotes.
- **Errors by exception.** Domain errors derive from `KvError`; `execute()` catches
  them in one place and formats `(error) ...`.

### 5. Testable time

`ExpiryManager` takes a `Clock` (`std::function<int64_t()>` returning milliseconds).
Production uses `steady_clock`, which is monotonic and immune to wall-clock changes.
Tests inject `FakeClock` and call `advanceSec(3)` instead of sleeping.

## Complexity

| Operation | Average | Worst case | Notes |
|---|---|---|---|
| `SET`, `GET`, `DEL`, `EXISTS`, `INCR` | O(1) | O(n) | worst case: hash collisions / rehash |
| `SADD`, `SREM`, `SISMEMBER`, `SCARD` | O(1) per member | O(n) | same |
| `EXPIRE`, `SET ... EX` | O(log n) | O(n) | heap push; occasional O(n) compaction |
| `TTL`, `PERSIST` | O(1) | O(n) | map lookup / erase |
| `SMEMBERS` | O(m) | O(m) | m = set size |
| `KEYS` | O(n) | O(n) | full scan, as in Redis |
| Active sweep | O(k log n) | | k = keys expired |

"O(1) average" is the honest claim for hash tables: a bad hash or a rehash can cost
O(n) for a single operation.

## Testing

```bash
./build/kv_tests
```

75 tests cover each layer in isolation and end to end:

- **Storage:** overwrite, wrong-type errors, empty-set removal, glob edge cases.
- **Expiry:** inclusive boundary (`now == deadline` is expired), extending and
  shortening TTLs, stale-heap handling, bounded heap growth, overflow saturation.
- **Database:** TTL conventions, `SET` clearing TTL, `INCR` keeping it, TTLs not leaking
  to recreated keys, overflow errors, active-sweep batching.
- **Parser:** quoting and escapes, arity errors, reply formats, full command sessions.

The suite also passes under `-fsanitize=address,undefined`:

```bash
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -Iinclude -Itests \
    src/StorageEngine.cpp src/ExpiryManager.cpp src/Database.cpp src/CommandParser.cpp \
    tests/*.cpp -o kv_tests_san && ./kv_tests_san
```

## Benchmark

```bash
./build/bench 1000000
```

Example run (1,000,000 keys, Release build, one core of a cloud VM; **your numbers
will differ**, so run it on your own machine and quote those):

| Operation | ops/sec |
|---|---|
| `GET` (hit) | ~4.6 M |
| `GET` (miss) | ~4.1 M |
| `INCR` (hot counters) | ~9.7 M |
| `SET` (1M new keys) | ~1.4 M |
| `SET` with TTL | ~0.8 M |
| `SADD` (one big set) | ~0.8 M |
| `DEL` | ~0.7 M |
| Active sweep of 1M expired keys | ~0.8 M keys/sec |
| `SET` via the full parser | ~1.7 M |
| `GET` via the full parser | ~2.2 M |

Inserts are slower than reads because they allocate memory and trigger rehashing.
