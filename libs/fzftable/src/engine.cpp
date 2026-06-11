#include "fzftable/engine.hpp"

#include "fzfmatch/chars.hpp"
#include "fzfmatch/normalize.hpp"
#include "fzfmatch/query.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fzftable {

namespace {

std::string join_strings(const std::vector<std::string> &parts,
                         std::string_view                separator) {
    std::string result;
    bool first = true;
    for (const auto &part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!first) {
            result += separator;
        }
        result += part;
        first = false;
    }
    return result;
}

bool is_hidden_keyword(std::string_view keyword) {
    return !keyword.empty() && keyword[0] == '@';
}

std::string visible_comment(const std::vector<std::string> &keywords) {
    std::vector<std::string> visible;
    visible.reserve(keywords.size());
    for (const auto &keyword : keywords) {
        if (!is_hidden_keyword(keyword)) {
            visible.push_back(keyword);
        }
    }
    return join_strings(visible, ", ");
}

Row parse_row(std::string_view line) {
    Row row;
    std::size_t start = 0;
    bool first = true;
    while (start <= line.size()) {
        const auto tab = line.find('\t', start);
        const auto end =
            tab == std::string_view::npos ? line.size() : tab;
        auto field = line.substr(start, end - start);
        if (first) {
            row.value.assign(field);
            first = false;
        } else if (!field.empty()) {
            row.keywords.emplace_back(field);
        }
        if (tab == std::string_view::npos) {
            break;
        }
        start = tab + 1;
    }
    row.joined_keywords = join_strings(row.keywords, " ");
    row.comment = visible_comment(row.keywords);
    return row;
}

std::vector<std::u32string> build_fields(const Row &row) {
    std::vector<std::u32string> fields;
    fields.reserve(2 + row.keywords.size());
    fields.push_back(fzfmatch::decode_utf8(row.value));
    if (!row.joined_keywords.empty()) {
        fields.push_back(fzfmatch::decode_utf8(row.joined_keywords));
    }
    for (const auto &keyword : row.keywords) {
        if (!keyword.empty()) {
            fields.push_back(fzfmatch::decode_utf8(keyword));
        }
    }
    return fields;
}

constexpr int16_t kFlatPerChar = 16;

std::u32string prepare_field(std::u32string_view    field,
                             const fzfmatch::Query::Term &term,
                             fzfmatch::Options      opts) {
    std::u32string folded;
    folded.reserve(field.size());
    for (char32_t c : field) {
        if (!term.case_sensitive) {
            c = fzfmatch::to_lower(c);
        }
        if (opts.normalize) {
            c = fzfmatch::normalize_rune(c);
        }
        folded.push_back(c);
    }
    return folded;
}

fzfmatch::FuzzyResult substring_match(std::u32string_view needle,
                                      std::u32string_view haystack) {
    fzfmatch::FuzzyResult result;
    if (needle.empty()) {
        result.matched = true;
        return result;
    }
    const auto pos = haystack.find(needle);
    if (pos == std::u32string_view::npos) {
        return result;
    }
    result.matched = true;
    result.score =
        static_cast<int16_t>(kFlatPerChar * static_cast<int>(needle.size()));
    result.start = static_cast<int32_t>(pos);
    result.end = static_cast<int32_t>(pos + needle.size());
    return result;
}

fzfmatch::FuzzyResult prefix_match(std::u32string_view needle,
                                   std::u32string_view haystack) {
    fzfmatch::FuzzyResult result;
    if (needle.size() > haystack.size()) {
        return result;
    }
    if (haystack.compare(0, needle.size(), needle) != 0) {
        return result;
    }
    result.matched = true;
    result.score =
        static_cast<int16_t>(kFlatPerChar * static_cast<int>(needle.size()) +
                             4);
    result.start = 0;
    result.end = static_cast<int32_t>(needle.size());
    return result;
}

fzfmatch::FuzzyResult suffix_match(std::u32string_view needle,
                                   std::u32string_view haystack) {
    fzfmatch::FuzzyResult result;
    if (needle.size() > haystack.size()) {
        return result;
    }
    const auto start = haystack.size() - needle.size();
    if (haystack.compare(start, needle.size(), needle) != 0) {
        return result;
    }
    result.matched = true;
    result.score =
        static_cast<int16_t>(kFlatPerChar * static_cast<int>(needle.size()) +
                             4);
    result.start = static_cast<int32_t>(start);
    result.end = static_cast<int32_t>(haystack.size());
    return result;
}

fzfmatch::FuzzyResult equal_match(std::u32string_view needle,
                                  std::u32string_view haystack) {
    fzfmatch::FuzzyResult result;
    if (needle != haystack) {
        return result;
    }
    result.matched = true;
    result.score =
        static_cast<int16_t>(kFlatPerChar * static_cast<int>(needle.size()) +
                             8);
    result.start = 0;
    result.end = static_cast<int32_t>(haystack.size());
    return result;
}

fzfmatch::FuzzyResult match_non_inverse_term_on_field(
    const fzfmatch::Query::Term &term, std::u32string_view field,
    fzfmatch::Options opts) {
    fzfmatch::FuzzyResult result;
    if (term.text.empty()) {
        result.matched = true;
        return result;
    }

    const auto prepared = prepare_field(field, term, opts);
    switch (term.mode) {
    case fzfmatch::Query::Mode::Fuzzy:
        return fzfmatch::fuzzy_match_v2(term.text, prepared, term.case_sensitive,
                                        false, opts);
    case fzfmatch::Query::Mode::Exact:
        return substring_match(term.text, prepared);
    case fzfmatch::Query::Mode::Prefix:
        return prefix_match(term.text, prepared);
    case fzfmatch::Query::Mode::Suffix:
        return suffix_match(term.text, prepared);
    case fzfmatch::Query::Mode::Equal:
        return equal_match(term.text, prepared);
    }
    return result;
}

fzfmatch::FuzzyResult match_term_across_fields(
    const fzfmatch::Query::Term &term, const std::vector<std::u32string> &fields,
    fzfmatch::Options opts) {
    if (term.text.empty()) {
        fzfmatch::FuzzyResult result;
        result.matched = !term.inverse;
        return result;
    }

    if (term.inverse) {
        for (const auto &field : fields) {
            auto match = match_non_inverse_term_on_field(term, field, opts);
            if (match.matched) {
                return {};
            }
        }
        fzfmatch::FuzzyResult result;
        result.matched = true;
        return result;
    }

    fzfmatch::FuzzyResult best;
    bool matched = false;
    for (const auto &field : fields) {
        auto match = match_non_inverse_term_on_field(term, field, opts);
        if (!match.matched) {
            continue;
        }
        if (!matched || match.score > best.score) {
            best = std::move(match);
            matched = true;
        }
    }
    if (!matched) {
        return {};
    }
    return best;
}

fzfmatch::FuzzyResult match_query_on_fields(
    const fzfmatch::Query &query, const std::vector<std::u32string> &fields) {
    fzfmatch::FuzzyResult combined;
    combined.matched = true;
    int combined_score = 0;
    for (const auto &group : query.ast()) {
        fzfmatch::FuzzyResult best_group;
        bool matched_group = false;
        for (const auto &term : group) {
            auto match =
                match_term_across_fields(term, fields, query.options());
            if (!match.matched) {
                continue;
            }
            if (!matched_group || match.score > best_group.score) {
                best_group = std::move(match);
                matched_group = true;
            }
        }
        if (!matched_group) {
            return {};
        }
        combined_score += best_group.score;
    }

    if (!combined.matched) {
        return {};
    }

    combined.score = static_cast<int16_t>(std::clamp(
        combined_score, static_cast<int>(std::numeric_limits<int16_t>::min()),
        static_cast<int>(std::numeric_limits<int16_t>::max())));
    if (query.ast().empty()) {
        combined.start = 0;
        combined.end = 0;
    }
    return combined;
}

} // namespace

Table Table::load_file(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("unable to open table: " + path.string());
    }
    return load_stream(input, path.filename().string());
}

Table Table::load_stream(std::istream &input, std::string name) {
    Table table;
    table.name_ = std::move(name);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto row = parse_row(line);
        if (row.value.empty()) {
            continue;
        }
        table.fields_.push_back(build_fields(row));
        table.rows_.push_back(std::move(row));
    }
    return table;
}

std::vector<Match> Table::search(std::string_view query,
                                 SearchOptions    options) const {
    auto parsed = fzfmatch::Query::parse(query, options.match_options);

    std::vector<Match> matches;
    matches.reserve(rows_.size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto result = match_query_on_fields(parsed, fields_[i]);
        if (!result.matched) {
            continue;
        }
        matches.push_back(Match{
            .index = i,
            .score = result.score,
            .row = &rows_[i],
            .result = std::move(result),
        });
    }

    std::sort(matches.begin(), matches.end(),
              [](const Match &lhs, const Match &rhs) {
                  if (lhs.score != rhs.score) {
                      return lhs.score > rhs.score;
                  }
                  return lhs.index < rhs.index;
              });

    if (options.limit != 0 && matches.size() > options.limit) {
        matches.resize(options.limit);
    }
    return matches;
}

void Session::set_table(const Table *table) {
    table_ = table;
    clear();
}

bool Session::append(std::string_view utf8) {
    if (utf8.empty()) {
        return false;
    }
    query_.append(utf8);
    return true;
}

bool Session::backspace() {
    if (query_.empty()) {
        return false;
    }
    const auto offsets = fzfmatch::codepoint_to_byte_offsets(query_);
    if (offsets.size() < 2) {
        query_.clear();
        return true;
    }
    query_.erase(offsets[offsets.size() - 2]);
    return true;
}

void Session::clear() {
    query_.clear();
    matches_.clear();
}

void Session::rerank(SearchOptions options) {
    options_ = options;
    if (!table_ || query_.empty()) {
        matches_.clear();
        return;
    }
    matches_ = table_->search(query_, options_);
}

} // namespace fzftable
