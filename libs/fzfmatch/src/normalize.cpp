#include "fzfmatch/normalize.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace fzfmatch {

namespace {

// Sorted by source codepoint. Generated from external/fzf/src/algo/normalize.go
// at commit v0.65.2 via awk + sort, see PLAN.md.
constexpr std::array<std::pair<char32_t, char32_t>, 496> kNormalizeTable{{
#include "normalize_table.inc"
}};

// fzf's normalizeRune guards: c in [0x00C0, 0xFF61] before lookup.
constexpr char32_t kNormalizeLo = 0x00C0;
constexpr char32_t kNormalizeHi = 0xFF61;

} // namespace

char32_t normalize_rune(char32_t c) noexcept {
    if (c < kNormalizeLo || c > kNormalizeHi) return c;
    auto it = std::lower_bound(
        kNormalizeTable.begin(), kNormalizeTable.end(), c,
        [](const auto& e, char32_t k) { return e.first < k; });
    if (it != kNormalizeTable.end() && it->first == c) return it->second;
    return c;
}

std::u32string normalize(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t c : s) out.push_back(normalize_rune(c));
    return out;
}

char32_t to_lower(char32_t c) noexcept {
    // ASCII fast path.
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c < 0x80) return c;
    // Latin-1: U+00C0..U+00DE except U+00D7 (multiplication sign). The matching
    // lowercase block is U+00E0..U+00FE.
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return c + 32;
    // Latin Extended-A (U+0100..U+017F): pairs alternate A-Z, but spacing is
    // mostly even=upper / odd=lower except a handful (0x0130, 0x0178). Cover
    // the common pattern; the remaining mapped codepoints are handled by
    // normalize_rune which folds to ASCII anyway.
    if (c >= 0x0100 && c <= 0x0137) return c | 1;          // even -> odd
    if (c >= 0x0139 && c <= 0x0148) return ((c - 0x0139) & 1) ? c : c + 1;
    if (c >= 0x014A && c <= 0x0177) return c | 1;
    if (c == 0x0178) return 0x00FF;
    return c;
}

std::u32string to_lower(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t c : s) out.push_back(to_lower(c));
    return out;
}

} // namespace fzfmatch
