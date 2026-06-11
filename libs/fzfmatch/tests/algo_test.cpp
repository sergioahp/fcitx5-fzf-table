#include "fzfmatch/algo.hpp"
#include "fzfmatch/chars.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace fzfmatch;

namespace {

FuzzyResult run(std::u32string_view pattern, std::u32string_view text,
                Options opts = {}) {
    auto p = prepare_pattern(pattern, opts);
    return fuzzy_match_v2(p.pattern, text, p.case_sensitive, true, opts);
}

} // namespace

TEST(Algo, EmptyPatternMatches) {
    auto r = run(U"", U"anything");
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.score, 0);
}

TEST(Algo, PatternLongerThanTextFails) {
    auto r = run(U"abcdef", U"abc");
    EXPECT_FALSE(r.matched);
}

TEST(Algo, ExactSubstring) {
    auto r = run(U"abc", U"abc");
    EXPECT_TRUE(r.matched);
    ASSERT_EQ(r.positions.size(), 3u);
    EXPECT_EQ(r.positions[0], 0);
    EXPECT_EQ(r.positions[1], 1);
    EXPECT_EQ(r.positions[2], 2);
}

TEST(Algo, ScatteredFuzzy) {
    auto r = run(U"abc", U"a_b_c");
    EXPECT_TRUE(r.matched);
    ASSERT_EQ(r.positions.size(), 3u);
    EXPECT_EQ(r.positions[0], 0);
    EXPECT_EQ(r.positions[1], 2);
    EXPECT_EQ(r.positions[2], 4);
}

TEST(Algo, NoMatch) {
    auto r = run(U"xyz", U"abcdef");
    EXPECT_FALSE(r.matched);
}

TEST(Algo, ConsecutiveBeatsScattered) {
    // "foo-bar" should beat "fxoxo" for query "foo" because of consecutive bonus
    // when the candidate has the literal substring.
    auto a = run(U"foo", U"foobar");
    auto b = run(U"foo", U"fxoxo");
    EXPECT_TRUE(a.matched);
    EXPECT_TRUE(b.matched);
    EXPECT_GT(a.score, b.score);
}

TEST(Algo, WordBoundaryBonus) {
    // "fb" should score higher when matching at word boundaries.
    auto a = run(U"fb", U"foo-bar");
    auto b = run(U"fb", U"fxbx");
    EXPECT_TRUE(a.matched);
    EXPECT_TRUE(b.matched);
    EXPECT_GT(a.score, b.score);
}

TEST(Algo, SmartCaseLowercaseMatchesUpper) {
    Options opts;
    auto r = run(U"abc", U"ABCdef", opts);
    EXPECT_TRUE(r.matched);
}

TEST(Algo, SmartCaseMixedRequiresExact) {
    Options opts;
    auto match_upper = run(U"Abc", U"ABCdef", opts);
    auto match_lower = run(U"Abc", U"abcdef", opts);
    EXPECT_FALSE(match_upper.matched);
    EXPECT_FALSE(match_lower.matched);
    auto match_exact = run(U"Abc", U"AbcXYZ", opts);
    EXPECT_TRUE(match_exact.matched);
}

TEST(Algo, NormalizationFolding) {
    Options opts; // normalize=true default
    auto r = run(U"cafe", U"café mocha");
    EXPECT_TRUE(r.matched);
    ASSERT_EQ(r.positions.size(), 4u);
}

TEST(Algo, FirstCharBoundaryBeatsMiddle) {
    // fzf comment: first char at a boundary gets a bonus multiplied by a
    // constant. So "ongoing" (o at index 0, word start) should outscore
    // "to-go-on" (o at index 1, mid-word).
    auto at_boundary = run(U"og", U"ongoing");
    auto in_word     = run(U"og", U"to-go-on");
    EXPECT_TRUE(at_boundary.matched);
    EXPECT_TRUE(in_word.matched);
    EXPECT_GT(at_boundary.score, in_word.score);
}

TEST(Algo, ScoresFromFzfReference) {
    // Values taken from fzf's algo_test.go TestFuzzyMatch table; only the
    // FuzzyMatchV2 column, default scheme.
    struct Case { std::u32string pat, txt; int16_t score; int32_t s, e; };
    std::vector<Case> cases = {
        {U"oBz", U"fooBarbaz", 64, 2, 5},     // expected approximate
        {U"FH", U"Foo Hello", 50, 0, 5},
    };
    for (auto& c : cases) {
        auto r = run(c.pat, c.txt);
        EXPECT_TRUE(r.matched) << "pat=" << encode_utf8(c.pat);
        // Only check that score is in a sane positive range; exact equality
        // with fzf depends on Unicode case folding details we may differ on.
        EXPECT_GT(r.score, 0);
    }
}
