#include "fzfmatch/chars.hpp"

#include <gtest/gtest.h>

using namespace fzfmatch;

TEST(Chars, AsciiRoundTrip) {
    std::string s = "hello world";
    auto cps = decode_utf8(s);
    EXPECT_EQ(cps.size(), s.size());
    EXPECT_EQ(encode_utf8(cps), s);
}

TEST(Chars, MultiByteRoundTrip) {
    // "café 日本 🐱"
    std::string s = "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x90\xB1";
    auto cps = decode_utf8(s);
    ASSERT_EQ(cps.size(), 9u);
    EXPECT_EQ(cps[3], 0x00E9);  // é
    EXPECT_EQ(cps[5], 0x65E5);  // 日
    EXPECT_EQ(cps[6], 0x672C);  // 本
    EXPECT_EQ(cps[8], 0x1F431); // 🐱
    EXPECT_EQ(encode_utf8(cps), s);
}

TEST(Chars, InvalidLeadingByte) {
    // Concatenate literals so the hex escape doesn't absorb the following 'b'.
    std::string s = "a\xFF" "b"; // 0xFF is never valid as a UTF-8 leading byte
    auto cps = decode_utf8(s);
    ASSERT_EQ(cps.size(), 3u);
    EXPECT_EQ(cps[0], U'a');
    EXPECT_EQ(cps[1], 0xFFFD);
    EXPECT_EQ(cps[2], U'b');
}

TEST(Chars, OverlongRejected) {
    // 0xC0 0xAF would be overlong "/", reject.
    std::string s = "\xC0\xAF";
    auto cps = decode_utf8(s);
    EXPECT_EQ(cps.front(), 0xFFFD);
}

TEST(Chars, CodepointToByteOffsets) {
    std::string s = "a\xC3\xA9\xE6\x97\xA5"; // a, é, 日
    auto off = codepoint_to_byte_offsets(s);
    ASSERT_EQ(off.size(), 4u);
    EXPECT_EQ(off[0], 0u);
    EXPECT_EQ(off[1], 1u);
    EXPECT_EQ(off[2], 3u);
    EXPECT_EQ(off[3], 6u);
}

TEST(Chars, IsAscii) {
    EXPECT_TRUE(is_ascii(U"plain"));
    EXPECT_FALSE(is_ascii(U"café"));
}
