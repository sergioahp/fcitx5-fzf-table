#include "fzfmatch/query.hpp"
#include "fzfmatch/algo.hpp"
#include "fzfmatch/chars.hpp"
#include "fzfmatch/normalize.hpp"

#include <algorithm>
#include <cctype>

namespace fzfmatch {

namespace {

bool is_space(char c) noexcept { return c == ' ' || c == '\t'; }

std::vector<std::string_view> tokenize(std::string_view expr) {
    std::vector<std::string_view> tokens;
    size_t i = 0, n = expr.size();
    while (i < n) {
        while (i < n && is_space(expr[i])) ++i;
        size_t start = i;
        while (i < n && !is_space(expr[i])) ++i;
        if (start < i) tokens.emplace_back(expr.substr(start, i - start));
    }
    return tokens;
}

// Classify one raw token into a Term. Handles "!" inverse and the four mode
// prefix/suffix sigils.
Query::Term parse_term(std::string_view raw, Options opts) {
    Query::Term t;
    if (raw.empty()) return t;
    if (raw.front() == '!') {
        t.inverse = true;
        raw.remove_prefix(1);
        if (raw.empty()) return t;
    }

    char prefix = raw.front();
    if (prefix == '\'') {
        t.mode = Query::Mode::Exact;
        raw.remove_prefix(1);
    } else if (prefix == '^') {
        t.mode = Query::Mode::Prefix;
        raw.remove_prefix(1);
    } else if (prefix == '=') {
        t.mode = Query::Mode::Equal;
        raw.remove_prefix(1);
    } else if (raw.size() > 1 && raw.back() == '$') {
        t.mode = Query::Mode::Suffix;
        raw.remove_suffix(1);
    }

    auto pat = decode_utf8(raw);
    auto prepared = prepare_pattern(pat, opts);
    t.text           = std::move(prepared.pattern);
    t.case_sensitive = prepared.case_sensitive;
    return t;
}

// Simple flat scoring for non-fuzzy modes. Score grows with pattern length so
// longer matches outrank shorter ones, but stays below what FuzzyMatchV2 can
// produce on tight matches so the modes remain comparable.
constexpr int16_t kFlatPerChar = 16;

FuzzyResult substring_match(const Query::Term& t, std::u32string_view text) {
    FuzzyResult R;
    if (t.text.empty()) { R.matched = true; return R; }
    auto pos = text.find(t.text);
    if (pos == std::u32string_view::npos) return R;
    R.matched = true;
    R.score   = static_cast<int16_t>(kFlatPerChar * t.text.size());
    R.start   = static_cast<int32_t>(pos);
    R.end     = R.start + static_cast<int32_t>(t.text.size());
    R.positions.reserve(t.text.size());
    for (int32_t i = R.start; i < R.end; ++i) R.positions.push_back(i);
    return R;
}

FuzzyResult prefix_match(const Query::Term& t, std::u32string_view text) {
    FuzzyResult R;
    if (t.text.size() > text.size()) return R;
    if (text.compare(0, t.text.size(), t.text) != 0) return R;
    R.matched = true;
    R.score   = static_cast<int16_t>(kFlatPerChar * t.text.size() + 4);
    R.start   = 0;
    R.end     = static_cast<int32_t>(t.text.size());
    R.positions.reserve(t.text.size());
    for (int32_t i = 0; i < R.end; ++i) R.positions.push_back(i);
    return R;
}

FuzzyResult suffix_match(const Query::Term& t, std::u32string_view text) {
    FuzzyResult R;
    if (t.text.size() > text.size()) return R;
    auto offset = text.size() - t.text.size();
    if (text.compare(offset, t.text.size(), t.text) != 0) return R;
    R.matched = true;
    R.score   = static_cast<int16_t>(kFlatPerChar * t.text.size() + 4);
    R.start   = static_cast<int32_t>(offset);
    R.end     = static_cast<int32_t>(text.size());
    R.positions.reserve(t.text.size());
    for (int32_t i = R.start; i < R.end; ++i) R.positions.push_back(i);
    return R;
}

FuzzyResult equal_match(const Query::Term& t, std::u32string_view text) {
    FuzzyResult R;
    if (t.text != text) return R;
    R.matched = true;
    R.score   = static_cast<int16_t>(kFlatPerChar * t.text.size() + 8);
    R.start   = 0;
    R.end     = static_cast<int32_t>(text.size());
    R.positions.reserve(text.size());
    for (int32_t i = 0; i < R.end; ++i) R.positions.push_back(i);
    return R;
}

FuzzyResult match_term(const Query::Term& t, std::u32string_view text,
                       Options opts) {
    FuzzyResult R;
    if (t.text.empty()) {
        R.matched = !t.inverse;
        return R;
    }
    // Lowercase + normalize the text the same way the term was prepared so
    // direct string comparisons are consistent for non-fuzzy modes.
    std::u32string folded;
    folded.reserve(text.size());
    for (char32_t c : text) {
        if (!t.case_sensitive) c = to_lower(c);
        if (opts.normalize)    c = normalize_rune(c);
        folded.push_back(c);
    }
    std::u32string_view view{folded};
    switch (t.mode) {
        case Query::Mode::Fuzzy:
            R = fuzzy_match_v2(t.text, view, t.case_sensitive, true, opts);
            break;
        case Query::Mode::Exact:
            R = substring_match(t, view); break;
        case Query::Mode::Prefix:
            R = prefix_match(t, view); break;
        case Query::Mode::Suffix:
            R = suffix_match(t, view); break;
        case Query::Mode::Equal:
            R = equal_match(t, view); break;
    }
    if (t.inverse) {
        bool was = R.matched;
        R = FuzzyResult{};
        R.matched = !was;
    }
    return R;
}

} // namespace

Query Query::parse(std::string_view expr, Options opts) {
    Query q;
    q.opts_ = opts;
    auto tokens = tokenize(expr);
    Group current;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string_view tok = tokens[i];
        if (tok == "|") {
            // Reach-back: the previous token should already be in `current`.
            // The next token (if any) joins the same OR-group.
            continue;  // handled by lookahead below
        }
        bool is_or_continuation = (i > 0 && tokens[i - 1] == "|");
        if (is_or_continuation) {
            current.push_back(parse_term(tok, opts));
        } else {
            if (!current.empty()) q.ast_.push_back(std::move(current));
            current.clear();
            current.push_back(parse_term(tok, opts));
        }
    }
    if (!current.empty()) q.ast_.push_back(std::move(current));
    return q;
}

FuzzyResult Query::match(std::u32string_view text) const {
    FuzzyResult combined;
    combined.matched = true;
    for (const auto& group : ast_) {
        FuzzyResult best;
        bool any = false;
        for (const auto& term : group) {
            auto r = match_term(term, text, opts_);
            if (!r.matched) continue;
            if (!any || r.score > best.score) { best = r; any = true; }
        }
        if (!any) return FuzzyResult{}; // AND fails
        combined.score += best.score;
        if (combined.start < 0) combined.start = best.start;
        if (best.end > combined.end) combined.end = best.end;
        combined.positions.insert(combined.positions.end(),
                                  best.positions.begin(), best.positions.end());
    }
    if (ast_.empty()) {
        // Empty query matches everything with neutral score.
        combined.start = 0; combined.end = 0;
    } else {
        std::sort(combined.positions.begin(), combined.positions.end());
        combined.positions.erase(
            std::unique(combined.positions.begin(), combined.positions.end()),
            combined.positions.end());
    }
    return combined;
}

FuzzyResult Query::match(std::string_view utf8_text) const {
    auto t = decode_utf8(utf8_text);
    return match(std::u32string_view{t});
}

} // namespace fzfmatch
