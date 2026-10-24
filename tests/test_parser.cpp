#include <string>
#include <vector>

#include "CommandParser.h"
#include "Database.h"
#include "fake_clock.h"
#include "test_framework.h"

using kv::CommandParser;
using tf::V;

namespace {

// A parser wired to a Database that runs on a fake clock.
struct Fixture {
    FakeClock fc;
    kv::Database db;
    CommandParser p;
    Fixture() : db(fc.clock()), p(db) {}
    std::string run(const std::string& line) { return p.execute(line); }
};

std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    std::string err;
    CHECK(CommandParser::tokenize(line, out, err));
    return out;
}

const char* kWrongType = "(error) WRONGTYPE Operation against a key holding the wrong kind of value";
const char* kNotInt = "(error) ERR value is not an integer or out of range";

}  // namespace

// ---- tokenizer ---------------------------------------------------------------

TEST(Parser_TokenizeBasics) {
    CHECK_EQ(tokens("SET a 1"), V({"SET", "a", "1"}));
    CHECK_EQ(tokens("   SET    a   1   "), V({"SET", "a", "1"}));
    CHECK_EQ(tokens("").size(), 0);
    CHECK_EQ(tokens("   ").size(), 0);
    CHECK_EQ(tokens("SET\ta\t1"), V({"SET", "a", "1"}));
}

TEST(Parser_TokenizeQuotes) {
    CHECK_EQ(tokens("SET msg \"hello world\""), V({"SET", "msg", "hello world"}));
    CHECK_EQ(tokens("SET msg 'hello world'"), V({"SET", "msg", "hello world"}));
    CHECK_EQ(tokens("SET k \"\""), V({"SET", "k", ""}));  // empty string is a token
    CHECK_EQ(tokens("SET k \"  padded  \""), V({"SET", "k", "  padded  "}));
    CHECK_EQ(tokens("SET k 'say \"hi\"'"), V({"SET", "k", "say \"hi\""}));
}

TEST(Parser_TokenizeEscapes) {
    CHECK_EQ(tokens("SET k \"a\\\"b\""), V({"SET", "k", "a\"b"}));
    CHECK_EQ(tokens("SET k \"line1\\nline2\""), V({"SET", "k", "line1\nline2"}));
    CHECK_EQ(tokens("SET k \"back\\\\slash\""), V({"SET", "k", "back\\slash"}));
    CHECK_EQ(tokens("SET k 'it\\'s'"), V({"SET", "k", "it's"}));
}

TEST(Parser_TokenizeUnbalancedQuotes) {
    std::vector<std::string> out;
    std::string err;
    CHECK(!CommandParser::tokenize("SET k \"oops", out, err));
    CHECK(!err.empty());
    CHECK(!CommandParser::tokenize("SET k 'oops", out, err));
    CHECK(!CommandParser::tokenize("SET k \"a\"b", out, err));  // junk after closing quote
}

// ---- dispatch / validation ------------------------------------------------------

TEST(Parser_BlankLineGivesEmptyReply) {
    Fixture f;
    CHECK_EQ(f.run(""), "");
    CHECK_EQ(f.run("    "), "");
}

TEST(Parser_CommandNamesAreCaseInsensitive) {
    Fixture f;
    CHECK_EQ(f.run("set k v"), "OK");
    CHECK_EQ(f.run("gEt k"), "\"v\"");
    CHECK_EQ(f.run("GET k"), "\"v\"");
}

TEST(Parser_UnknownCommand) {
    Fixture f;
    CHECK_EQ(f.run("foo bar"), "(error) ERR unknown command 'foo'");
}

TEST(Parser_WrongArgumentCount) {
    Fixture f;
    CHECK_EQ(f.run("GET"), "(error) ERR wrong number of arguments for 'get' command");
    CHECK_EQ(f.run("GET a b"), "(error) ERR wrong number of arguments for 'get' command");
    CHECK_EQ(f.run("SET a"), "(error) ERR wrong number of arguments for 'set' command");
    CHECK_EQ(f.run("DEL"), "(error) ERR wrong number of arguments for 'del' command");
    CHECK_EQ(f.run("SADD s"), "(error) ERR wrong number of arguments for 'sadd' command");
    CHECK_EQ(f.run("EXPIRE a"), "(error) ERR wrong number of arguments for 'expire' command");
    CHECK_EQ(f.run("DBSIZE x"), "(error) ERR wrong number of arguments for 'dbsize' command");
}

TEST(Parser_UnbalancedQuoteIsAnError) {
    Fixture f;
    CHECK_EQ(f.run("SET k \"oops"), "(error) ERR Protocol error: unbalanced quotes in request");
}

// ---- strings ----------------------------------------------------------------------

TEST(Parser_SetGetDel) {
    Fixture f;
    CHECK_EQ(f.run("SET name Rahul"), "OK");
    CHECK_EQ(f.run("GET name"), "\"Rahul\"");
    CHECK_EQ(f.run("GET nothing"), "(nil)");
    CHECK_EQ(f.run("EXISTS name"), "(integer) 1");
    CHECK_EQ(f.run("EXISTS name nothing"), "(integer) 1");
    CHECK_EQ(f.run("DEL name nothing"), "(integer) 1");
    CHECK_EQ(f.run("GET name"), "(nil)");
}

TEST(Parser_QuotedValuesRoundTrip) {
    Fixture f;
    CHECK_EQ(f.run("SET msg \"hello world\""), "OK");
    CHECK_EQ(f.run("GET msg"), "\"hello world\"");
    CHECK_EQ(f.run("SET q \"say \\\"hi\\\"\""), "OK");
    CHECK_EQ(f.run("GET q"), "\"say \\\"hi\\\"\"");  // printed escaped, like redis-cli
    CHECK_EQ(f.run("SET e \"\""), "OK");
    CHECK_EQ(f.run("GET e"), "\"\"");
}

TEST(Parser_SetWithExpiryOptions) {
    Fixture f;
    CHECK_EQ(f.run("SET tmp v EX 2"), "OK");
    CHECK_EQ(f.run("TTL tmp"), "(integer) 2");
    f.fc.advanceSec(3);
    CHECK_EQ(f.run("GET tmp"), "(nil)");

    CHECK_EQ(f.run("SET tmp2 v px 1500"), "OK");
    CHECK_EQ(f.run("PTTL tmp2"), "(integer) 1500");
}

TEST(Parser_SetOptionErrors) {
    Fixture f;
    CHECK_EQ(f.run("SET a b EX"), "(error) ERR syntax error");
    CHECK_EQ(f.run("SET a b XX 5"), "(error) ERR syntax error");
    CHECK_EQ(f.run("SET a b EX abc"), kNotInt);
    CHECK_EQ(f.run("SET a b EX 0"), "(error) ERR invalid expire time in 'set' command");
    CHECK_EQ(f.run("SET a b EX -5"), "(error) ERR invalid expire time in 'set' command");
    CHECK_EQ(f.run("GET a"), "(nil)");  // none of the failed SETs wrote anything
}

TEST(Parser_ExpireTtlPersist) {
    Fixture f;
    CHECK_EQ(f.run("TTL nothing"), "(integer) -2");
    f.run("SET a 1");
    CHECK_EQ(f.run("TTL a"), "(integer) -1");
    CHECK_EQ(f.run("EXPIRE a 10"), "(integer) 1");
    CHECK_EQ(f.run("TTL a"), "(integer) 10");
    CHECK_EQ(f.run("PERSIST a"), "(integer) 1");
    CHECK_EQ(f.run("TTL a"), "(integer) -1");
    CHECK_EQ(f.run("EXPIRE nothing 10"), "(integer) 0");
    CHECK_EQ(f.run("EXPIRE a abc"), kNotInt);
}

TEST(Parser_IncrDecr) {
    Fixture f;
    CHECK_EQ(f.run("INCR counter"), "(integer) 1");
    CHECK_EQ(f.run("INCR counter"), "(integer) 2");
    CHECK_EQ(f.run("INCRBY counter 10"), "(integer) 12");
    CHECK_EQ(f.run("DECR counter"), "(integer) 11");
    CHECK_EQ(f.run("DECRBY counter 11"), "(integer) 0");
    CHECK_EQ(f.run("GET counter"), "\"0\"");

    f.run("SET s abc");
    CHECK_EQ(f.run("INCR s"), kNotInt);
    CHECK_EQ(f.run("INCRBY counter xyz"), kNotInt);
    CHECK_EQ(f.run("DECRBY counter -9223372036854775808"),
             "(error) ERR decrement would overflow");

    f.run("SET big 9223372036854775807");
    CHECK_EQ(f.run("INCR big"), "(error) ERR increment or decrement would overflow");
}

// ---- sets -------------------------------------------------------------------------

TEST(Parser_SetCommands) {
    Fixture f;
    CHECK_EQ(f.run("SADD fruits apple banana apple"), "(integer) 2");
    CHECK_EQ(f.run("SCARD fruits"), "(integer) 2");
    CHECK_EQ(f.run("SMEMBERS fruits"), "1) \"apple\"\n2) \"banana\"");
    CHECK_EQ(f.run("SISMEMBER fruits apple"), "(integer) 1");
    CHECK_EQ(f.run("SISMEMBER fruits cherry"), "(integer) 0");
    CHECK_EQ(f.run("TYPE fruits"), "set");
    CHECK_EQ(f.run("SREM fruits apple banana"), "(integer) 2");
    CHECK_EQ(f.run("EXISTS fruits"), "(integer) 0");
    CHECK_EQ(f.run("SMEMBERS fruits"), "(empty array)");
}

TEST(Parser_WrongTypeErrors) {
    Fixture f;
    f.run("SADD s a");
    f.run("SET str x");
    CHECK_EQ(f.run("GET s"), kWrongType);
    CHECK_EQ(f.run("INCR s"), kWrongType);
    CHECK_EQ(f.run("SADD str a"), kWrongType);
    CHECK_EQ(f.run("SMEMBERS str"), kWrongType);
    CHECK_EQ(f.run("SCARD str"), kWrongType);
}

// ---- keys / server ----------------------------------------------------------------

TEST(Parser_KeysTypeDbsizeFlush) {
    Fixture f;
    f.run("SET user:2 b");
    f.run("SET user:1 a");
    f.run("SET order:1 c");
    CHECK_EQ(f.run("KEYS user:*"), "1) \"user:1\"\n2) \"user:2\"");
    CHECK_EQ(f.run("KEYS nomatch*"), "(empty array)");
    CHECK_EQ(f.run("TYPE user:1"), "string");
    CHECK_EQ(f.run("TYPE nothing"), "none");
    CHECK_EQ(f.run("DBSIZE"), "(integer) 3");
    CHECK_EQ(f.run("FLUSHALL"), "OK");
    CHECK_EQ(f.run("DBSIZE"), "(integer) 0");
}

TEST(Parser_PingAndHelp) {
    Fixture f;
    CHECK_EQ(f.run("PING"), "PONG");
    CHECK_EQ(f.run("PING hi"), "\"hi\"");
    const std::string help = f.run("HELP");
    CHECK(help.find("SET") != std::string::npos);
    CHECK(help.find("SMEMBERS") != std::string::npos);
}

TEST(Parser_EndToEndExpirySession) {
    Fixture f;
    CHECK_EQ(f.run("SET a 1"), "OK");
    CHECK_EQ(f.run("EXPIRE a 2"), "(integer) 1");
    f.fc.advanceSec(3);
    CHECK_EQ(f.run("GET a"), "(nil)");
    CHECK_EQ(f.run("EXISTS a"), "(integer) 0");
    CHECK_EQ(f.run("TTL a"), "(integer) -2");
}
