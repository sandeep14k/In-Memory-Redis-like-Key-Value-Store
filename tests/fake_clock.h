#pragma once

#include <cstdint>

#include "ExpiryManager.h"

// A clock the test controls. Hand `clock()` to Database / ExpiryManager, then
// call advance*() instead of sleeping, so TTL tests are instant and exact.
// Must outlive whatever it was handed to.
struct FakeClock {
    std::int64_t nowMs = 1000;

    kv::ExpiryManager::Clock clock() {
        return [this] { return nowMs; };
    }
    void advanceMs(std::int64_t ms) { nowMs += ms; }
    void advanceSec(std::int64_t s) { nowMs += s * 1000; }
};
