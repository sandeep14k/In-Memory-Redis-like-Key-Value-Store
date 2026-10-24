#include <cstdint>
#include <limits>

#include "ExpiryManager.h"
#include "fake_clock.h"
#include "test_framework.h"

using kv::ExpiryManager;
using tf::V;

TEST(Expiry_NoExpiryByDefault) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    CHECK(!em.hasExpiry("a"));
    CHECK_EQ(em.ttlMs("a"), -1);
    CHECK(!em.isExpired("a"));
}

TEST(Expiry_BoundaryIsInclusive) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    em.setExpiry("a", 1000);
    CHECK_EQ(em.ttlMs("a"), 1000);

    fc.advanceMs(999);
    CHECK(!em.isExpired("a"));  // 1 ms left
    CHECK_EQ(em.ttlMs("a"), 1);

    fc.advanceMs(1);
    CHECK(em.isExpired("a"));  // now == deadline -> expired
    CHECK_EQ(em.ttlMs("a"), 0);
}

TEST(Expiry_RemoveExpiry) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    em.setExpiry("a", 1000);
    CHECK(em.removeExpiry("a"));
    CHECK(!em.removeExpiry("a"));
    CHECK_EQ(em.ttlMs("a"), -1);
    fc.advanceSec(10);
    CHECK(!em.isExpired("a"));
    CHECK_EQ(em.popExpired().size(), 0);  // stale heap entry must not resurrect it
}

TEST(Expiry_PopExpiredReturnsDueKeysOnly) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    em.setExpiry("a", 1000);
    em.setExpiry("b", 2000);
    em.setExpiry("c", 3000);

    fc.advanceMs(2000);
    CHECK_EQ(tf::sorted(em.popExpired()), V({"a", "b"}));
    CHECK_EQ(em.size(), 1);
    CHECK(em.hasExpiry("c"));
    CHECK(!em.hasExpiry("a"));
    CHECK_EQ(em.popExpired().size(), 0);  // nothing left that is due
}

TEST(Expiry_PopExpiredRespectsLimit) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    for (int i = 0; i < 10; ++i) em.setExpiry("k" + std::to_string(i), 100);
    fc.advanceMs(100);
    CHECK_EQ(em.popExpired(3).size(), 3);
    CHECK_EQ(em.size(), 7);
    CHECK_EQ(em.popExpired(0).size(), 7);  // 0 = no limit
    CHECK_EQ(em.size(), 0);
}

TEST(Expiry_ExtendingTtlIgnoresStaleHeapEntry) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    em.setExpiry("a", 1000);  // heap now has (1000, a)
    em.setExpiry("a", 5000);  // heap also has (5000, a); the first one is stale

    fc.advanceMs(2000);
    CHECK_EQ(em.popExpired().size(), 0);  // stale entry popped and dropped, key survives
    CHECK(em.hasExpiry("a"));

    fc.advanceMs(3000);
    CHECK_EQ(em.popExpired(), V({"a"}));
}

TEST(Expiry_ShorteningTtlWorks) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    em.setExpiry("a", 10000);
    em.setExpiry("a", 500);
    fc.advanceMs(500);
    CHECK_EQ(em.popExpired(), V({"a"}));
}

TEST(Expiry_HeapStaysBoundedWhenTtlIsRewrittenManyTimes) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    for (int i = 0; i < 10000; ++i) em.setExpiry("hot", 1000 + i);
    CHECK_EQ(em.size(), 1);
    CHECK(em.heapSize() <= 70);  // compaction kept it near the live set size
}

TEST(Expiry_HeapStaysBoundedWhenKeysAreRemoved) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    for (int i = 0; i < 5000; ++i) {
        const std::string k = "k" + std::to_string(i);
        em.setExpiry(k, 1000);
        em.removeExpiry(k);
    }
    CHECK_EQ(em.size(), 0);
    CHECK(em.heapSize() <= 70);
}

TEST(Expiry_HugeTtlSaturatesInsteadOfOverflowing) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    em.setExpiry("a", std::numeric_limits<std::int64_t>::max());
    CHECK(!em.isExpired("a"));
    fc.advanceSec(1000000);
    CHECK(!em.isExpired("a"));
}

TEST(Expiry_Clear) {
    FakeClock fc;
    ExpiryManager em(fc.clock());
    em.setExpiry("a", 100);
    em.clear();
    CHECK_EQ(em.size(), 0);
    CHECK_EQ(em.heapSize(), 0);
    fc.advanceMs(1000);
    CHECK_EQ(em.popExpired().size(), 0);
}

TEST(Expiry_SystemClockIsMonotonic) {
    ExpiryManager em;  // real clock
    const auto a = em.now();
    const auto b = em.now();
    CHECK(b >= a);
}
