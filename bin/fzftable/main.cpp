// fzftable: headless table search using libfzfmatch.
//
// Reads a TSV table (value<TAB>keyword[<TAB>keyword...] per line), scores each
// row against a query, prints ranked results to stdout.
//
// Output format:    <score>\t<value>\t<visible keywords joined by ", ">
// One row per line. Empty stdout if no matches.

#include "fzfmatch/algo.hpp"
#include "fzfmatch/query.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool isHiddenKeyword(const std::string& keyword) {
    return !keyword.empty() && keyword[0] == '@';
}

struct Row {
    std::string              value;
    std::vector<std::string> keywords;
    std::string              haystack; // value + " " + keywords joined
};

std::vector<Row> load_table(const std::string& path) {
    std::vector<Row> rows;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "fzftable: cannot open %s\n", path.c_str());
        std::exit(2);
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        Row r;
        size_t start = 0, tab;
        bool first = true;
        while ((tab = line.find('\t', start)) != std::string::npos) {
            if (first) { r.value = line.substr(start, tab - start); first = false; }
            else       { r.keywords.emplace_back(line.substr(start, tab - start)); }
            start = tab + 1;
        }
        if (first) r.value = line.substr(start);
        else       r.keywords.emplace_back(line.substr(start));

        r.haystack = r.value;
        for (auto& kw : r.keywords) { r.haystack.push_back(' '); r.haystack += kw; }
        rows.push_back(std::move(r));
    }
    return rows;
}

struct Args {
    std::string              table;
    std::string              query;
    int                      limit = 20;
    bool                     show_positions = false;
};

[[noreturn]] void usage(int code = 2) {
    std::fputs(
        "Usage: fzftable --table=<file> --query=<expr> [--limit=N] [--positions]\n",
        stderr);
    std::exit(code);
}

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string_view s = argv[i];
        if      (s.starts_with("--table=")) a.table = s.substr(8);
        else if (s.starts_with("--query=")) a.query = s.substr(8);
        else if (s.starts_with("--limit=")) std::from_chars(s.data() + 8, s.data() + s.size(), a.limit);
        else if (s == "--positions")       a.show_positions = true;
        else if (s == "-h" || s == "--help") usage(0);
        else { std::fprintf(stderr, "fzftable: unknown arg: %.*s\n",
                            int(s.size()), s.data()); usage(); }
    }
    if (a.table.empty() || a.query.empty()) usage();
    return a;
}

} // namespace

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    auto rows = load_table(args.table);
    auto q    = fzfmatch::Query::parse(args.query);

    struct Scored { size_t idx; fzfmatch::FuzzyResult r; };
    std::vector<Scored> hits;
    hits.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        auto r = q.match(rows[i].haystack);
        if (r.matched) hits.push_back({i, std::move(r)});
    }
    std::sort(hits.begin(), hits.end(),
              [](const Scored& a, const Scored& b) {
                  if (a.r.score != b.r.score) return a.r.score > b.r.score;
                  return a.idx < b.idx;
              });
    if (static_cast<int>(hits.size()) > args.limit)
        hits.resize(args.limit);
    for (const auto& h : hits) {
        const auto& row = rows[h.idx];
        std::cout << h.r.score << '\t' << row.value << '\t';
        bool first = true;
        for (size_t k = 0; k < row.keywords.size(); ++k) {
            if (isHiddenKeyword(row.keywords[k])) continue;
            if (!first) std::cout << ", ";
            first = false;
            std::cout << row.keywords[k];
        }
        if (args.show_positions) {
            std::cout << '\t';
            for (size_t k = 0; k < h.r.positions.size(); ++k) {
                if (k > 0) std::cout << ',';
                std::cout << h.r.positions[k];
            }
        }
        std::cout << '\n';
    }
    return 0;
}
