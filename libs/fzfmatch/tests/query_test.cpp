#include "fzfmatch/chars.hpp"
#include "fzfmatch/query.hpp"

#include <gtest/gtest.h>

using namespace fzfmatch;

TEST(Query, SingleFuzzy) {
    auto q = Query::parse("cat");
    auto r = q.match("black cat");
    EXPECT_TRUE(r.matched);
}

TEST(Query, AndOfTwoTerms) {
    auto q = Query::parse("cat black");
    EXPECT_TRUE(q.match("black cat").matched);
    EXPECT_FALSE(q.match("cat").matched);
}

TEST(Query, OrAlternation) {
    auto q = Query::parse("cat | dog");
    EXPECT_TRUE(q.match("cat").matched);
    EXPECT_TRUE(q.match("dog").matched);
    EXPECT_FALSE(q.match("fish").matched);
}

TEST(Query, ExactSubstring) {
    auto q = Query::parse("'cat");
    EXPECT_TRUE(q.match("scatter").matched);
    EXPECT_FALSE(q.match("c_a_t").matched);
}

TEST(Query, PrefixOnly) {
    auto q = Query::parse("^cat");
    EXPECT_TRUE(q.match("cat in the hat").matched);
    EXPECT_FALSE(q.match("black cat").matched);
}

TEST(Query, SuffixOnly) {
    auto q = Query::parse("cat$");
    EXPECT_TRUE(q.match("a cat").matched);
    EXPECT_FALSE(q.match("cats").matched);
}

TEST(Query, EqualMatch) {
    auto q = Query::parse("=cat");
    EXPECT_TRUE(q.match("cat").matched);
    EXPECT_FALSE(q.match("cats").matched);
}

TEST(Query, InverseFuzzy) {
    auto q = Query::parse("!cat");
    EXPECT_TRUE(q.match("dog").matched);
    EXPECT_FALSE(q.match("cat").matched);
}

TEST(Query, InverseCombinedWithMode) {
    auto q = Query::parse("!^cat");
    EXPECT_TRUE(q.match("black cat").matched);  // doesn't start with cat
    EXPECT_FALSE(q.match("cat in hat").matched); // does start with cat
}

TEST(Query, EmptyQueryMatchesEverything) {
    auto q = Query::parse("");
    EXPECT_TRUE(q.match("anything").matched);
}

TEST(Query, SmartCasePerTerm) {
    auto q = Query::parse("Cat dog"); // first term has uppercase -> case-sensitive
    EXPECT_TRUE(q.match("Cat and dog").matched);
    EXPECT_FALSE(q.match("cat and dog").matched);
}

TEST(Query, AndWithOrMixed) {
    auto q = Query::parse("cat | dog fish");
    EXPECT_TRUE(q.match("a cat and a fish").matched);
    EXPECT_TRUE(q.match("dog and fish").matched);
    EXPECT_FALSE(q.match("cat only").matched);
    EXPECT_FALSE(q.match("fish only").matched);
}

TEST(Query, UnicodeNormalize) {
    auto q = Query::parse("cafe");
    EXPECT_TRUE(q.match("caf\xC3\xA9 mocha").matched);
}
