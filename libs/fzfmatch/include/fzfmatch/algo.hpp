#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fzfmatch {

// Whether the matcher folds case while scoring. fzf calls this "caseSensitive"
// in algo.go; we invert the polarity so the default in our struct is "Smart".
enum class CaseMode : uint8_t {
    Smart,    // case-sensitive iff pattern has any uppercase
    Ignore,   // always case-insensitive
    Respect,  // always case-sensitive
};

struct Options {
    CaseMode case_mode = CaseMode::Smart;
    bool normalize     = true;
    bool forward       = true;
};

struct FuzzyResult {
    bool                 matched = false;
    int16_t              score   = 0;
    int32_t              start   = -1;  // codepoint index, first matched
    int32_t              end     = -1;  // codepoint index, one-past-last
    std::vector<int32_t> positions;     // codepoint indices, ascending; empty
                                        // unless caller asked for them
};

// fzf-style scoring. Pattern must already be lowercased (if !case_sensitive)
// and normalized (if opts.normalize is true). Use prepare_pattern() to do that.
FuzzyResult fuzzy_match_v2(std::u32string_view pattern,
                           std::u32string_view text,
                           bool                case_sensitive,
                           bool                with_positions = true,
                           Options             opts           = {});

// Returns (prepared_pattern, case_sensitive). Smart-case rule: if input has any
// uppercase codepoint then case_sensitive is true and pattern is returned as
// given (but normalized if opts.normalize); else pattern is lowercased.
struct PreparedPattern {
    std::u32string pattern;
    bool           case_sensitive;
};
PreparedPattern prepare_pattern(std::u32string_view pattern, Options opts = {});

} // namespace fzfmatch
