#include <cstdint>
#include <limits>

#include "Database.h"
#include "fake_clock.h"
#include "test_framework.h"

using kv::Database;
using kv::KvError;
using kv::NotIntegerError;
using kv::WrongTypeError;
using tf::sorted;
using tf::V;

namespace {
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
}  // namespace

TEST(Database_SetGetDel) {
    FakeClock fc;
    Database db(fc.clock());
    CHECK(!db.get("a"));
    db.set("a", "1");
    CHECK_EQ(db.get("a"), "1");
    CHECK_EQ(db.del({"a"}), 1);
    CHECK(!db.get("a"));
    CHECK_EQ(db.del({"a"}), 0);
}

TEST(Database_DelCountsEachExistingKeyOnce) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1");
    db.set("b", "2");
    CHECK_EQ(db.del({"a", "b", "c", "a"}), 2);  // c missing, second "a" already gone
}

TEST(Database_ExistsCountsDuplicates) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1");
    CHECK_EQ(db.exists({"a", "a", "zzz"}), 2);
}

// The example from the project plan: SET a 1, EXPIRE a 2, wait 3s, GET a -> nil.
TEST(Database_ExpireThenGetReturnsNilAfterTimeout) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1");
    CHECK(db.expire("a", 2));
    CHECK_EQ(db.get("a"), "1");
    fc.advanceSec(3);
    CHECK(!db.get("a"));
}

TEST(Database_SetWithTtlBoundary) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 2000);
    fc.advanceMs(1999);
    CHECK_EQ(db.get("a"), "1");
    fc.advanceMs(1);
    CHECK(!db.get("a"));
}

TEST(Database_TtlConventions) {
    FakeClock fc;
    Database db(fc.clock());
    CHECK_EQ(db.ttl("missing"), -2);
    db.set("a", "1");
    CHECK_EQ(db.ttl("a"), -1);
    db.expire("a", 10);
    CHECK_EQ(db.ttl("a"), 10);
    CHECK_EQ(db.pttl("a"), 10000);
    fc.advanceMs(8600);  // 1400 ms left -> rounds to 1 s
    CHECK_EQ(db.ttl("a"), 1);
    fc.advanceMs(100);  // 1300 left; still 1
    CHECK_EQ(db.ttl("a"), 1);
    fc.advanceMs(800);  // 500 left -> rounds up to 1
    CHECK_EQ(db.ttl("a"), 1);
    fc.advanceMs(1);  // 499 left -> rounds to 0
    CHECK_EQ(db.ttl("a"), 0);
    fc.advanceMs(499);  // expired
    CHECK_EQ(db.ttl("a"), -2);
}

TEST(Database_PlainSetClearsTtl) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 1000);
    db.set("a", "2");  // overwrite without TTL
    CHECK_EQ(db.ttl("a"), -1);
    fc.advanceSec(10);
    CHECK_EQ(db.get("a"), "2");
}

TEST(Database_SetWithNewTtlReplacesOld) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 1000);
    db.set("a", "2", 5000);
    fc.advanceMs(2000);
    CHECK_EQ(db.get("a"), "2");
    fc.advanceMs(3000);
    CHECK(!db.get("a"));
}

// A deleted key's TTL must not leak onto a new key with the same name.
TEST(Database_DelClearsExpiry) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 1000);
    db.del({"a"});
    db.set("a", "new");
    CHECK_EQ(db.ttl("a"), -1);
    fc.advanceSec(5);
    CHECK_EQ(db.get("a"), "new");
}

TEST(Database_ExpiredKeyIsNotCountedByDelOrExists) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 1000);
    fc.advanceSec(2);
    CHECK_EQ(db.exists({"a"}), 0);
    db.set("b", "1", 1000);
    fc.advanceSec(2);
    CHECK_EQ(db.del({"b"}), 0);
}

TEST(Database_ExpireOnMissingKeyReturnsFalse) {
    FakeClock fc;
    Database db(fc.clock());
    CHECK(!db.expire("nope", 10));
}

TEST(Database_NonPositiveExpireDeletesKey) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1");
    CHECK(db.expire("a", 0));
    CHECK(!db.get("a"));
    db.set("b", "1");
    CHECK(db.expire("b", -5));
    CHECK(!db.get("b"));
}

TEST(Database_ExpireWithHugeValueDoesNotOverflow) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1");
    CHECK(db.expire("a", kMax));
    fc.advanceSec(100000);
    CHECK_EQ(db.get("a"), "1");
}

TEST(Database_Persist) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 1000);
    CHECK(db.persist("a"));
    CHECK(!db.persist("a"));  // already persistent
    CHECK(!db.persist("missing"));
    fc.advanceSec(10);
    CHECK_EQ(db.get("a"), "1");
}

TEST(Database_IncrDecr) {
    FakeClock fc;
    Database db(fc.clock());
    CHECK_EQ(db.incrBy("n", 1), 1);  // missing key starts at 0
    CHECK_EQ(db.incrBy("n", 1), 2);
    CHECK_EQ(db.incrBy("n", 10), 12);
    CHECK_EQ(db.incrBy("n", -20), -8);
    CHECK_EQ(db.get("n"), "-8");
}

TEST(Database_IncrKeepsTtl) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("n", "5", 10000);
    db.incrBy("n", 1);
    CHECK_EQ(db.ttl("n"), 10);
    fc.advanceSec(11);
    CHECK(!db.get("n"));
}

TEST(Database_IncrOnExpiredKeyRestartsFromZero) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("n", "41", 1000);
    fc.advanceSec(2);
    CHECK_EQ(db.incrBy("n", 1), 1);
    CHECK_EQ(db.ttl("n"), -1);
}

TEST(Database_IncrErrors) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("s", "hello");
    CHECK_THROWS(db.incrBy("s", 1), NotIntegerError);
    db.set("f", "1.5");
    CHECK_THROWS(db.incrBy("f", 1), NotIntegerError);
    db.set("sp", " 5");
    CHECK_THROWS(db.incrBy("sp", 1), NotIntegerError);
    db.set("max", std::to_string(kMax));
    CHECK_THROWS(db.incrBy("max", 1), KvError);
    db.set("min", std::to_string(kMin));
    CHECK_THROWS(db.incrBy("min", -1), KvError);
    CHECK_EQ(db.get("max"), std::to_string(kMax));  // failed INCR leaves value untouched
    db.sadd("set", V({"x"}));
    CHECK_THROWS(db.incrBy("set", 1), WrongTypeError);
}

TEST(Database_WrongType) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("str", "x");
    db.sadd("set", V({"m"}));
    CHECK_THROWS(db.get("set"), WrongTypeError);
    CHECK_THROWS(db.sadd("str", V({"m"})), WrongTypeError);
    CHECK_THROWS(db.smembers("str"), WrongTypeError);
}

TEST(Database_SetCommands) {
    FakeClock fc;
    Database db(fc.clock());
    CHECK_EQ(db.sadd("s", V({"a", "b", "c"})), 3);
    CHECK_EQ(db.scard("s"), 3);
    CHECK(db.sismember("s", "b"));
    CHECK_EQ(db.srem("s", V({"b"})), 1);
    CHECK(!db.sismember("s", "b"));
    CHECK_EQ(sorted(db.smembers("s")), V({"a", "c"}));
    CHECK_EQ(db.type("s"), "set");
}

TEST(Database_SetKeysExpireToo) {
    FakeClock fc;
    Database db(fc.clock());
    db.sadd("s", V({"a"}));
    db.expire("s", 5);
    fc.advanceSec(6);
    CHECK_EQ(db.smembers("s").size(), 0);
    CHECK_EQ(db.scard("s"), 0);
    CHECK(!db.sismember("s", "a"));
    CHECK_EQ(db.type("s"), "none");
}

// Removing the last member deletes the key; its TTL must go with it.
TEST(Database_SremOfLastMemberClearsExpiry) {
    FakeClock fc;
    Database db(fc.clock());
    db.sadd("s", V({"a"}));
    db.expire("s", 5);
    db.srem("s", V({"a"}));
    db.sadd("s", V({"b"}));  // brand-new set under the same name
    CHECK_EQ(db.ttl("s"), -1);
    fc.advanceSec(60);
    CHECK_EQ(db.smembers("s"), V({"b"}));
}

TEST(Database_OverwritingSetWithStringChangesType) {
    FakeClock fc;
    Database db(fc.clock());
    db.sadd("k", V({"m"}));
    db.set("k", "v");
    CHECK_EQ(db.type("k"), "string");
    CHECK_EQ(db.get("k"), "v");
}

TEST(Database_KeysSkipsExpired) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("user:1", "a");
    db.set("user:2", "b", 1000);
    db.set("order:1", "c");
    fc.advanceSec(2);
    CHECK_EQ(sorted(db.keys("user:*")), V({"user:1"}));
    CHECK_EQ(sorted(db.keys("*")), V({"order:1", "user:1"}));
}

TEST(Database_ActiveExpirationReclaimsWithoutAccess) {
    FakeClock fc;
    Database db(fc.clock());
    for (int i = 0; i < 5; ++i) db.set("k" + std::to_string(i), "v", 1000);
    db.set("keep", "v");
    fc.advanceSec(2);
    CHECK_EQ(db.activeExpireCycle(), 5);  // nobody touched those keys; the sweep found them
    CHECK_EQ(db.dbsize(), 1);
    CHECK_EQ(db.get("keep"), "v");
    CHECK_EQ(db.activeExpireCycle(), 0);
}

TEST(Database_ActiveExpirationHonoursBatchLimit) {
    FakeClock fc;
    Database db(fc.clock());
    for (int i = 0; i < 10; ++i) db.set("k" + std::to_string(i), "v", 100);
    fc.advanceMs(200);
    CHECK_EQ(db.activeExpireCycle(4), 4);
    CHECK_EQ(db.activeExpireCycle(4), 4);
    CHECK_EQ(db.activeExpireCycle(4), 2);
}

TEST(Database_DbsizeExcludesExpired) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 1000);
    db.set("b", "1");
    CHECK_EQ(db.dbsize(), 2);
    fc.advanceSec(2);
    CHECK_EQ(db.dbsize(), 1);
}

TEST(Database_FlushAllClearsDataAndExpiry) {
    FakeClock fc;
    Database db(fc.clock());
    db.set("a", "1", 1000);
    db.sadd("s", V({"x"}));
    db.flushAll();
    CHECK_EQ(db.dbsize(), 0);
    db.set("a", "again");  // must not inherit the old TTL
    fc.advanceSec(60);
    CHECK_EQ(db.get("a"), "again");
}

TEST(Database_TypeOfMissingKey) {
    FakeClock fc;
    Database db(fc.clock());
    CHECK_EQ(db.type("nope"), "none");
    db.set("s", "x");
    CHECK_EQ(db.type("s"), "string");
}

TEST(Database_ManyKeys) {
    FakeClock fc;
    Database db(fc.clock());
    for (int i = 0; i < 10000; ++i) db.set("k" + std::to_string(i), std::to_string(i));
    CHECK_EQ(db.dbsize(), 10000);
    CHECK_EQ(db.get("k9999"), "9999");
    CHECK_EQ(db.get("k0"), "0");
}
