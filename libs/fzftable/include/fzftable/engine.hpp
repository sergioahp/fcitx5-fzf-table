#pragma once

#include "fzfmatch/algo.hpp"

#include <cstddef>
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace fzftable {

struct Row {
    std::string              value;
    std::vector<std::string> keywords;
    std::string              joined_keywords;
    std::string              comment;
};

struct SearchOptions {
    std::size_t       limit = 10;
    fzfmatch::Options match_options = {};
};

struct Match {
    std::size_t           index = 0;
    int                   score = 0;
    const Row *           row = nullptr;
    fzfmatch::FuzzyResult result = {};
};

class Table {
public:
    static Table load_file(const std::filesystem::path &path);
    static Table load_stream(std::istream &input, std::string name = {});

    const std::string &name() const noexcept { return name_; }
    const std::vector<Row> &rows() const noexcept { return rows_; }

    std::vector<Match> search(std::string_view query,
                              SearchOptions    options = {}) const;

private:
    std::string                              name_;
    std::vector<Row>                         rows_;
    std::vector<std::vector<std::u32string>> fields_;
};

class Session {
public:
    explicit Session(const Table *table = nullptr) : table_(table) {}

    void set_table(const Table *table);

    const Table *table() const noexcept { return table_; }
    const std::string &query() const noexcept { return query_; }
    const std::vector<Match> &matches() const noexcept { return matches_; }

    bool append(std::string_view utf8);
    bool backspace();
    void clear();
    void rerank(SearchOptions options = {});

private:
    const Table *       table_ = nullptr;
    std::string         query_;
    std::vector<Match>  matches_;
    SearchOptions       options_ = {};
};

} // namespace fzftable
