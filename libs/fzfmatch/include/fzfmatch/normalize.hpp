#pragma once

#include <string>
#include <string_view>

namespace fzfmatch {

// Fold a single codepoint to its ASCII representative, if any. Identity for
// codepoints outside the fold table (see normalize.cpp). The table is the
// straight port of fzf's util/normalize.go and covers latin-script diacritics
// and CJK halfwidth/fullwidth ASCII clones.
char32_t normalize_rune(char32_t c) noexcept;

std::u32string normalize(std::u32string_view s);

// Lowercase a codepoint. ASCII fast path, otherwise simple table for
// upper->lower in [0x0080, 0xFF61]. Good enough for our IME use case where the
// candidate text is short and language-tagged.
char32_t to_lower(char32_t c) noexcept;
std::u32string to_lower(std::u32string_view s);

} // namespace fzfmatch
