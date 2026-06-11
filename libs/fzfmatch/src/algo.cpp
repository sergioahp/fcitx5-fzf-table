// FuzzyMatchV2 port of fzf v0.65.2 src/algo/algo.go.
//
// We mirror the upstream variable names (M, N, H, C, B, F, T, H0, C0) so the
// recurrence is comparable line-by-line. Behavioral differences:
//   * Input is already in codepoints (u32string_view), so we drop the
//     asciiFuzzyIndex byte-level prefilter.
//   * We skip FuzzyMatchV1 fallback (no slab arena pressure with our small
//     candidate counts).
//   * Lowercasing of non-ASCII chars covers Latin-1 + Latin Extended-A. Beyond
//     that, full Unicode case folding is delegated to the normalize table,
//     which already folds diacritic letters of both cases to ASCII.
//
// All scoring constants and bonus structure are intentionally identical.

#include "fzfmatch/algo.hpp"
#include "fzfmatch/normalize.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace fzfmatch {

namespace {

constexpr int16_t kScoreMatch               = 16;
constexpr int16_t kScoreGapStart            = -3;
constexpr int16_t kScoreGapExtension        = -1;
constexpr int16_t kBonusBoundary            = kScoreMatch / 2;
constexpr int16_t kBonusNonWord             = kScoreMatch / 2;
constexpr int16_t kBonusCamel123            = kBonusBoundary + kScoreGapExtension;
constexpr int16_t kBonusConsecutive         = -(kScoreGapStart + kScoreGapExtension);
constexpr int16_t kBonusBoundaryWhite       = kBonusBoundary + 2;
constexpr int16_t kBonusBoundaryDelimiter   = kBonusBoundary + 1;
constexpr int16_t kBonusFirstCharMultiplier = 2;

enum class CharClass : uint8_t {
    White,
    NonWord,
    Delimiter,
    Lower,
    Upper,
    Letter,
    Number,
};

constexpr std::string_view kWhiteChars     = " \t\n\v\f\r\x85\xA0";
constexpr std::string_view kDelimiterChars = "/,:;|";

constexpr CharClass kInitialCharClass = CharClass::White;

// Precomputed ASCII char class table.
struct AsciiClasses {
    std::array<CharClass, 128> data{};
    constexpr AsciiClasses() {
        for (int i = 0; i < 128; ++i) {
            char c = static_cast<char>(i);
            CharClass cls = CharClass::NonWord;
            if (c >= 'a' && c <= 'z') cls = CharClass::Lower;
            else if (c >= 'A' && c <= 'Z') cls = CharClass::Upper;
            else if (c >= '0' && c <= '9') cls = CharClass::Number;
            else if (kWhiteChars.find(c) != std::string_view::npos) cls = CharClass::White;
            else if (kDelimiterChars.find(c) != std::string_view::npos) cls = CharClass::Delimiter;
            data[i] = cls;
        }
    }
};
constexpr AsciiClasses kAsciiClasses{};

CharClass char_class_of_non_ascii(char32_t c) noexcept {
    // Cheap Latin-1 / Latin Extended-A subset. Sufficient for our IME corpora
    // (English descriptions + Latin diacritics + CJK/symbols). Codepoints not
    // explicitly classified default to Letter, which produces the same
    // "in-word" scoring behavior as Lower/Upper.
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return CharClass::Upper;
    if (c >= 0x00DF && c <= 0x00FF && c != 0x00F7) return CharClass::Lower;
    if (c >= 0x0100 && c <= 0x017F) {
        // Even codepoints in Latin Extended-A are mostly uppercase, odd lowercase.
        return (c & 1) ? CharClass::Lower : CharClass::Upper;
    }
    if (c == 0x3000) return CharClass::White;  // ideographic space
    return CharClass::Letter;
}

CharClass char_class_of(char32_t c) noexcept {
    if (c < 128) return kAsciiClasses.data[c];
    return char_class_of_non_ascii(c);
}

constexpr int16_t bonus_for(CharClass prev, CharClass cur) noexcept {
    if (cur > CharClass::NonWord) {
        switch (prev) {
            case CharClass::White:     return kBonusBoundaryWhite;
            case CharClass::Delimiter: return kBonusBoundaryDelimiter;
            case CharClass::NonWord:   return kBonusBoundary;
            default: break;
        }
    }
    if ((prev == CharClass::Lower && cur == CharClass::Upper) ||
        (prev != CharClass::Number && cur == CharClass::Number)) {
        return kBonusCamel123;
    }
    if (cur == CharClass::NonWord || cur == CharClass::Delimiter) return kBonusNonWord;
    if (cur == CharClass::White) return kBonusBoundaryWhite;
    return 0;
}

// Bonus matrix indexed by class enum value. ASCII path uses it directly.
struct BonusMatrix {
    std::array<std::array<int16_t, 7>, 7> m{};
    constexpr BonusMatrix() {
        for (int i = 0; i <= static_cast<int>(CharClass::Number); ++i) {
            for (int j = 0; j <= static_cast<int>(CharClass::Number); ++j) {
                m[i][j] = bonus_for(static_cast<CharClass>(i),
                                    static_cast<CharClass>(j));
            }
        }
    }
};
constexpr BonusMatrix kBonusMatrix{};

inline int16_t bonus(CharClass prev, CharClass cur) noexcept {
    return kBonusMatrix.m[static_cast<int>(prev)][static_cast<int>(cur)];
}

inline char32_t transform(char32_t c, bool case_sensitive, bool do_normalize) noexcept {
    CharClass cls = char_class_of(c);
    if (!case_sensitive && cls == CharClass::Upper) c = to_lower(c);
    if (do_normalize && c >= 0x80) c = normalize_rune(c);
    return c;
}

inline int16_t max16(int16_t a, int16_t b) noexcept { return a > b ? a : b; }

} // namespace

PreparedPattern prepare_pattern(std::u32string_view pattern, Options opts) {
    bool case_sensitive = false;
    if (opts.case_mode == CaseMode::Respect) {
        case_sensitive = true;
    } else if (opts.case_mode == CaseMode::Smart) {
        for (char32_t c : pattern) {
            char32_t lo = to_lower(c);
            if (lo != c) { case_sensitive = true; break; }
        }
    }

    std::u32string p;
    p.reserve(pattern.size());
    for (char32_t c : pattern) {
        if (!case_sensitive) c = to_lower(c);
        if (opts.normalize)  c = normalize_rune(c);
        p.push_back(c);
    }
    return {std::move(p), case_sensitive};
}

FuzzyResult fuzzy_match_v2(std::u32string_view pattern,
                           std::u32string_view text,
                           bool                case_sensitive,
                           bool                with_positions,
                           Options             opts) {
    FuzzyResult R;
    const int32_t M = static_cast<int32_t>(pattern.size());
    const int32_t N = static_cast<int32_t>(text.size());
    if (M == 0) {
        R.matched = true;
        R.start = 0; R.end = 0;
        return R;
    }
    if (M > N) return R;

    // Phase 2: scan text. Build per-position bonus, first-occurrence array F,
    // and H0/C0 (M=1 row of the DP table).
    std::vector<int16_t> H0(N), C0(N), B(N);
    std::vector<int32_t> F(M);
    std::vector<char32_t> T(N);

    int16_t   max_score   = 0;
    int32_t   max_score_pos = 0;
    int32_t   pidx        = 0;
    int32_t   last_idx    = 0;
    char32_t  pchar0      = pattern[0];
    char32_t  pchar       = pattern[0];
    int16_t   prev_h0     = 0;
    CharClass prev_class  = kInitialCharClass;
    bool      in_gap      = false;

    for (int32_t off = 0; off < N; ++off) {
        char32_t c = text[off];
        CharClass cls = char_class_of(c);
        c = transform(c, case_sensitive, opts.normalize);
        T[off] = c;

        int16_t bo = bonus(prev_class, cls);
        B[off] = bo;
        prev_class = cls;

        if (c == pchar) {
            if (pidx < M) {
                F[pidx] = off;
                ++pidx;
                pchar = pattern[std::min(pidx, M - 1)];
            }
            last_idx = off;
        }

        if (c == pchar0) {
            int16_t score = kScoreMatch + bo * kBonusFirstCharMultiplier;
            H0[off] = score;
            C0[off] = 1;
            if (M == 1 && ((opts.forward && score > max_score) ||
                            (!opts.forward && score >= max_score))) {
                max_score = score;
                max_score_pos = off;
                if (opts.forward && bo >= kBonusBoundary) break;
            }
            in_gap = false;
        } else {
            int16_t s = in_gap ? prev_h0 + kScoreGapExtension
                               : prev_h0 + kScoreGapStart;
            H0[off] = std::max<int16_t>(s, 0);
            C0[off] = 0;
            in_gap = true;
        }
        prev_h0 = H0[off];
    }
    if (pidx != M) return R;

    if (M == 1) {
        R.matched = true;
        R.score   = max_score;
        R.start   = max_score_pos;
        R.end     = max_score_pos + 1;
        if (with_positions) R.positions.push_back(max_score_pos);
        return R;
    }

    // Phase 3: fill the DP matrix H[width*M], C[width*M]. width = lastIdx-F[0]+1.
    const int32_t f0    = F[0];
    const int32_t width = last_idx - f0 + 1;
    std::vector<int16_t> H(static_cast<size_t>(width) * M, 0);
    std::vector<int16_t> C(static_cast<size_t>(width) * M, 0);
    std::memcpy(H.data(), H0.data() + f0, sizeof(int16_t) * width);
    std::memcpy(C.data(), C0.data() + f0, sizeof(int16_t) * width);

    for (int32_t row_idx = 1; row_idx < M; ++row_idx) {
        const int32_t  f   = F[row_idx];
        const char32_t pc  = pattern[row_idx];
        const int32_t  row = row_idx * width;
        bool           gap = false;
        // Hleft[0] anchor.
        H[row + f - f0 - 1] = 0;
        for (int32_t col = f; col <= last_idx; ++col) {
            const int32_t off = col - f;
            const int32_t k   = row + col - f0;
            int16_t s1 = 0, s2 = 0;
            int16_t consecutive = 0;

            s2 = gap ? H[k - 1] + kScoreGapExtension
                     : H[k - 1] + kScoreGapStart;

            if (T[col] == pc) {
                int16_t s_match = H[k - width - 1] + kScoreMatch;
                int16_t b       = B[col];
                consecutive     = C[k - width - 1] + 1;
                if (consecutive > 1) {
                    int16_t fb = B[col - consecutive + 1];
                    if (b >= kBonusBoundary && b > fb) {
                        consecutive = 1;
                    } else {
                        b = max16(b, max16(kBonusConsecutive, fb));
                    }
                }
                if (s_match + b < s2) {
                    s_match += B[col];
                    consecutive = 0;
                } else {
                    s_match += b;
                }
                s1 = s_match;
            }
            C[k] = consecutive;

            gap = s1 < s2;
            int16_t score = max16(max16(s1, s2), 0);
            if (row_idx == M - 1 &&
                ((opts.forward && score > max_score) ||
                 (!opts.forward && score >= max_score))) {
                max_score = score;
                max_score_pos = col;
            }
            H[k] = score;
        }
    }

    // Phase 4: optional backtrack.
    int32_t j = f0;
    if (with_positions) {
        R.positions.reserve(M);
        int32_t i = M - 1;
        j = max_score_pos;
        bool prefer_match = true;
        while (true) {
            const int32_t I  = i * width;
            const int32_t j0 = j - f0;
            const int16_t s  = H[I + j0];

            int16_t s1 = 0, s2 = 0;
            if (i > 0 && j >= F[i]) s1 = H[I - width + j0 - 1];
            if (j > F[i])           s2 = H[I + j0 - 1];

            if (s > s1 && (s > s2 || (s == s2 && prefer_match))) {
                R.positions.push_back(j);
                if (i == 0) break;
                --i;
            }
            const size_t below_idx = static_cast<size_t>(I) + width + j0 + 1;
            prefer_match = C[I + j0] > 1 || (below_idx < C.size() && C[below_idx] > 0);
            --j;
        }
        std::reverse(R.positions.begin(), R.positions.end());
    }
    R.matched = true;
    R.score   = max_score;
    R.start   = j;
    R.end     = max_score_pos + 1;
    return R;
}

} // namespace fzfmatch
