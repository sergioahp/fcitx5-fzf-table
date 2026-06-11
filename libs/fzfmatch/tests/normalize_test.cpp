#include "fzfmatch/normalize.hpp"

#include <gtest/gtest.h>

using namespace fzfmatch;

TEST(Normalize, IdentityBelowRange) {
    EXPECT_EQ(normalize_rune(U'a'), U'a');
    EXPECT_EQ(normalize_rune(0x00BF), 0x00BFu); // inverted question mark, just below range
}

TEST(Normalize, IdentityAboveRange) {
    EXPECT_EQ(normalize_rune(0xFF62), 0xFF62u);
    EXPECT_EQ(normalize_rune(0x1F600), 0x1F600u); // emoji unchanged
}

TEST(Normalize, KnownEntries) {
    EXPECT_EQ(normalize_rune(0x00C0), U'A'); // À
    EXPECT_EQ(normalize_rune(0x00E0), U'a'); // à
    EXPECT_EQ(normalize_rune(0x00E1), U'a'); // á
    EXPECT_EQ(normalize_rune(0x00E9), U'e'); // é
    EXPECT_EQ(normalize_rune(0x00F1), U'n'); // ñ
    EXPECT_EQ(normalize_rune(0x0103), U'a'); // a-with-breve, Latin Extended-A
    EXPECT_EQ(normalize_rune(0x010D), U'c'); // c-with-caron
    EXPECT_EQ(normalize_rune(0xFF07), U'\''); // fullwidth apostrophe
    EXPECT_EQ(normalize_rune(0xFF3C), U'\\'); // fullwidth backslash
}

TEST(Normalize, String) {
    EXPECT_EQ(normalize(U"café"), U"cafe");
    EXPECT_EQ(normalize(U"naïve"), U"naive");
}

TEST(Normalize, UnmappedInRangeIsIdentity) {
    // Pick a codepoint inside [0xC0, 0xFF61] that the fzf table does NOT
    // include. The Devanagari letter 'ka' (U+0915) is in-range and not in the
    // latin-only table.
    EXPECT_EQ(normalize_rune(0x0915), 0x0915u);
}

TEST(ToLower, AsciiAndLatin1) {
    EXPECT_EQ(to_lower(U'A'), U'a');
    EXPECT_EQ(to_lower(U'Z'), U'z');
    EXPECT_EQ(to_lower(U'a'), U'a');
    EXPECT_EQ(to_lower(0x00C0), 0x00E0u); // À -> à
    EXPECT_EQ(to_lower(0x00D6), 0x00F6u); // Ö -> ö
    EXPECT_EQ(to_lower(0x00D7), 0x00D7u); // × stays (mult sign)
}
