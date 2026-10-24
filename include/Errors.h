#pragma once

#include <stdexcept>
#include <string>

namespace kv {

// Base class for every error that should be reported to the client as
// "(error) <message>". Messages already carry the Redis-style prefix
// ("ERR ..." / "WRONGTYPE ...").
class KvError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Operation against a key that holds the wrong kind of value
// (e.g. GET on a set, SADD on a string).
class WrongTypeError : public KvError {
public:
    WrongTypeError()
        : KvError("WRONGTYPE Operation against a key holding the wrong kind of value") {}
};

// INCR / DECR on a value that is not a valid 64-bit integer.
class NotIntegerError : public KvError {
public:
    NotIntegerError() : KvError("ERR value is not an integer or out of range") {}
};

}  // namespace kv
