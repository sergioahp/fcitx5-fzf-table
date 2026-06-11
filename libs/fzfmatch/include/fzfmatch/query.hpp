#pragma once

#include "algo.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fzfmatch {

// A parsed fzf-style query. Terms separated by whitespace are ANDed; the `|`
// operator is OR within an AND-group. Each term has a Mode.
//
// Grammar (informal):
//   query  := and (WS and)*
//   and    := term (WS '|' WS term)*
//   term   := MODE_PREFIX? word MODE_SUFFIX?
//   MODE_PREFIX := "'" | "^" | "=" | "!"   (with ! combinable: !^, !', etc.)
//   MODE_SUFFIX := "$"
class Query {
public:
    enum class Mode : uint8_t {
        Fuzzy,
        Exact,
        Prefix,
        Suffix,
        Equal,
    };

    struct Term {
        Mode           mode    = Mode::Fuzzy;
        bool           inverse = false;
        std::u32string text;           // already normalized + case-folded per opts
        bool           case_sensitive; // smart-case decision per-term
    };

    using Group = std::vector<Term>; // OR within
    using Ast   = std::vector<Group>; // AND outer

    static Query parse(std::string_view expr, Options opts = {});

    // Returns combined score of best path through the AND-groups, or
    // FuzzyResult{matched=false} if any group fails.
    FuzzyResult match(std::u32string_view text) const;
    FuzzyResult match(std::string_view utf8_text) const;

    const Ast&     ast() const noexcept { return ast_; }
    const Options& options() const noexcept { return opts_; }

private:
    Ast     ast_;
    Options opts_;
};

} // namespace fzfmatch
