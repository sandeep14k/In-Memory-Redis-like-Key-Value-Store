#include "StorageEngine.h"
#include "test_framework.h"

using kv::StorageEngine;
using kv::WrongTypeError;
using tf::sorted;
using tf::V;

TEST(Storage_SetGetOverwrite) {
    StorageEngine s;
    CHECK(!s.getString("a"));
    s.setString("a", "1");
    CHECK_EQ(s.getString("a"), "1");
    s.setString("a", "2");
    CHECK_EQ(s.getString("a"), "2");
    CHECK_EQ(s.size(), 1);
}

TEST(Storage_EmptyStringIsAValue) {
    StorageEngine s;
    s.setString("e", "");
    CHECK(s.exists("e"));
    CHECK_EQ(s.getString("e"), "");
}

TEST(Storage_DelAndExists) {
    StorageEngine s;
    s.setString("a", "1");
    CHECK(s.exists("a"));
    CHECK(s.del("a"));
    CHECK(!s.exists("a"));
    CHECK(!s.del("a"));  // second delete: nothing to remove
    CHECK_EQ(s.size(), 0);
}

TEST(Storage_TypeReporting) {
    StorageEngine s;
    s.setString("str", "x");
    s.sadd("set", V({"m"}));
    CHECK(s.type("str") == StorageEngine::Type::String);
    CHECK(s.type("set") == StorageEngine::Type::Set);
    CHECK(s.type("nope") == StorageEngine::Type::None);
}

TEST(Storage_SaddCountsOnlyNewMembers) {
    StorageEngine s;
    CHECK_EQ(s.sadd("s", V({"a", "b", "a"})), 2);  // duplicate inside one call
    CHECK_EQ(s.sadd("s", V({"b", "c"})), 1);       // b already present
    CHECK_EQ(s.scard("s"), 3);
    CHECK_EQ(sorted(s.smembers("s")), V({"a", "b", "c"}));
}

TEST(Storage_SremAndEmptySetDisappears) {
    StorageEngine s;
    s.sadd("s", V({"a", "b"}));
    CHECK_EQ(s.srem("s", V({"a", "zzz"})), 1);
    CHECK(s.exists("s"));
    CHECK_EQ(s.srem("s", V({"b"})), 1);
    CHECK(!s.exists("s"));  // last member removed -> key gone
    CHECK_EQ(s.srem("s", V({"b"})), 0);
}

TEST(Storage_SismemberAndMissingKeys) {
    StorageEngine s;
    CHECK(!s.sismember("none", "x"));
    CHECK_EQ(s.scard("none"), 0);
    CHECK_EQ(s.smembers("none").size(), 0);
    s.sadd("s", V({"x"}));
    CHECK(s.sismember("s", "x"));
    CHECK(!s.sismember("s", "y"));
}

TEST(Storage_SaddWithNoMembersDoesNotCreateKey) {
    StorageEngine s;
    CHECK_EQ(s.sadd("s", {}), 0);
    CHECK(!s.exists("s"));
}

TEST(Storage_WrongType) {
    StorageEngine s;
    s.setString("str", "x");
    s.sadd("set", V({"m"}));

    CHECK_THROWS(s.getString("set"), WrongTypeError);
    CHECK_THROWS(s.sadd("str", V({"m"})), WrongTypeError);
    CHECK_THROWS(s.srem("str", V({"m"})), WrongTypeError);
    CHECK_THROWS(s.smembers("str"), WrongTypeError);
    CHECK_THROWS(s.sismember("str", "m"), WrongTypeError);
    CHECK_THROWS(s.scard("str"), WrongTypeError);
}

TEST(Storage_SetStringOverwritesAnyType) {
    StorageEngine s;
    s.sadd("k", V({"m"}));
    s.setString("k", "now a string");  // like Redis SET: type is replaced
    CHECK(s.type("k") == StorageEngine::Type::String);
    CHECK_EQ(s.getString("k"), "now a string");
}

TEST(Storage_KeysWithGlob) {
    StorageEngine s;
    for (const char* k : {"user:1", "user:2", "order:1", "hello", "hallo", "hxllo"}) {
        s.setString(k, "v");
    }
    CHECK_EQ(s.keys("*").size(), 6);
    CHECK_EQ(sorted(s.keys("user:*")), V({"user:1", "user:2"}));
    CHECK_EQ(sorted(s.keys("h?llo")), V({"hallo", "hello", "hxllo"}));
    CHECK_EQ(sorted(s.keys("*:1")), V({"order:1", "user:1"}));
    CHECK_EQ(s.keys("nomatch*").size(), 0);
}

TEST(Storage_GlobMatchEdgeCases) {
    CHECK(StorageEngine::globMatch("*", ""));
    CHECK(StorageEngine::globMatch("", ""));
    CHECK(!StorageEngine::globMatch("", "a"));
    CHECK(StorageEngine::globMatch("a*b*c", "aXXbYYc"));
    CHECK(StorageEngine::globMatch("a*b*c", "abc"));
    CHECK(!StorageEngine::globMatch("a*b*c", "abcd"));
    CHECK(StorageEngine::globMatch("**a", "xa"));
    CHECK(!StorageEngine::globMatch("?", ""));
    CHECK(StorageEngine::globMatch("a*", "a"));
    CHECK(StorageEngine::globMatch("*abc", "ababc"));  // needs backtracking
}

TEST(Storage_Clear) {
    StorageEngine s;
    s.setString("a", "1");
    s.sadd("b", V({"x"}));
    s.clear();
    CHECK_EQ(s.size(), 0);
    CHECK(!s.exists("a"));
}
