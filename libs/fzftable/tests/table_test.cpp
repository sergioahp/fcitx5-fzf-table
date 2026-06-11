#include "fzftable/engine.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace {

fzftable::Table load_table() {
    std::istringstream input(
        "# comment\n"
        "🐱\tcat face\tcat\tkitty\n"
        "🎉\tparty popper\tparty\tcelebration\n"
        "(T_T)\tcry face\tcry\tsad\n"
        "α\talpha\tgreek letter alpha\n");
    return fzftable::Table::load_stream(input, "test.tab");
}

TEST(FZFTableCore, LoadsRowsAndComments) {
    auto table = load_table();

    ASSERT_EQ(table.rows().size(), 4u);
    EXPECT_EQ(table.rows()[0].value, "🐱");
    EXPECT_EQ(table.rows()[0].joined_keywords, "cat face cat kitty");
    EXPECT_EQ(table.rows()[0].comment, "cat face, cat, kitty");
}

TEST(FZFTableCore, PrefixMatchesKeywordField) {
    auto table = load_table();

    auto matches = table.search("^party", {.limit = 5});

    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches.front().row->value, "🎉");
}

TEST(FZFTableCore, EqualMatchesValueOrSingleKeyword) {
    auto table = load_table();

    auto cry = table.search("=cry", {.limit = 5});
    ASSERT_FALSE(cry.empty());
    EXPECT_EQ(cry.front().row->value, "(T_T)");

    auto alpha = table.search("=α", {.limit = 5});
    ASSERT_FALSE(alpha.empty());
    EXPECT_EQ(alpha.front().row->value, "α");
}

TEST(FZFTableCore, SessionTracksUtf8QueryAndReranks) {
    auto table = load_table();

    fzftable::Session session(&table);
    EXPECT_TRUE(session.append("c"));
    EXPECT_TRUE(session.append("a"));
    session.rerank({.limit = 5});
    ASSERT_FALSE(session.matches().empty());
    EXPECT_EQ(session.matches().front().row->value, "🐱");

    EXPECT_TRUE(session.backspace());
    session.rerank({.limit = 5});
    ASSERT_FALSE(session.matches().empty());
    EXPECT_EQ(session.query(), "c");

    session.clear();
    session.rerank({.limit = 5});
    EXPECT_TRUE(session.matches().empty());
}

TEST(FZFTableCore, HiddenKeywordsStaySearchableButLeaveCommentsClean) {
    std::istringstream input("𝛂\t\\alpha\t@variant:bold\n");
    auto table = fzftable::Table::load_stream(input, "typst.tab");

    ASSERT_EQ(table.rows().size(), 1u);
    EXPECT_EQ(table.rows()[0].comment, "\\alpha");

    auto matches = table.search("@variant:bold", {.limit = 5});
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches.front().row->value, "𝛂");
}

} // namespace
