#include "fzfmatch/chars.hpp"

#include <vector>

namespace fzfmatch {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

// Returns codepoint + number of bytes consumed. On invalid input emits
// kReplacement and consumes exactly 1 byte. Treats overlong sequences as
// invalid per RFC 3629.
std::pair<char32_t, std::size_t> decode_one(const unsigned char* p, std::size_t n) noexcept {
    if (n == 0) return {kReplacement, 0};
    unsigned char b0 = p[0];
    if (b0 < 0x80) return {b0, 1};

    auto cont = [&](std::size_t i) -> int {
        if (i >= n) return -1;
        unsigned char b = p[i];
        if ((b & 0xC0) != 0x80) return -1;
        return b & 0x3F;
    };

    if ((b0 & 0xE0) == 0xC0) {
        int c1 = cont(1);
        if (c1 < 0) return {kReplacement, 1};
        char32_t cp = ((b0 & 0x1F) << 6) | static_cast<char32_t>(c1);
        if (cp < 0x80) return {kReplacement, 1};
        return {cp, 2};
    }
    if ((b0 & 0xF0) == 0xE0) {
        int c1 = cont(1), c2 = cont(2);
        if (c1 < 0 || c2 < 0) return {kReplacement, 1};
        char32_t cp = ((b0 & 0x0F) << 12) | (static_cast<char32_t>(c1) << 6) |
                       static_cast<char32_t>(c2);
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return {kReplacement, 1};
        return {cp, 3};
    }
    if ((b0 & 0xF8) == 0xF0) {
        int c1 = cont(1), c2 = cont(2), c3 = cont(3);
        if (c1 < 0 || c2 < 0 || c3 < 0) return {kReplacement, 1};
        char32_t cp = ((b0 & 0x07) << 18) | (static_cast<char32_t>(c1) << 12) |
                       (static_cast<char32_t>(c2) << 6) |
                       static_cast<char32_t>(c3);
        if (cp < 0x10000 || cp > 0x10FFFF) return {kReplacement, 1};
        return {cp, 4};
    }
    return {kReplacement, 1};
}

} // namespace

std::u32string decode_utf8(std::string_view src) {
    std::u32string out;
    out.reserve(src.size());  // upper bound
    auto* p = reinterpret_cast<const unsigned char*>(src.data());
    std::size_t n = src.size(), i = 0;
    while (i < n) {
        auto [cp, k] = decode_one(p + i, n - i);
        out.push_back(cp);
        i += k;
    }
    return out;
}

std::string encode_utf8(std::u32string_view src) {
    std::string out;
    out.reserve(src.size() * 2);
    for (char32_t cp : src) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = kReplacement;
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            // Invalid codepoint: emit U+FFFD.
            out.append("\xEF\xBF\xBD");
        }
    }
    return out;
}

std::vector<std::size_t> codepoint_to_byte_offsets(std::string_view utf8) {
    std::vector<std::size_t> out;
    out.reserve(utf8.size() + 1);
    auto* p = reinterpret_cast<const unsigned char*>(utf8.data());
    std::size_t n = utf8.size(), i = 0;
    while (i < n) {
        out.push_back(i);
        auto [_, k] = decode_one(p + i, n - i);
        i += k;
    }
    out.push_back(n);
    return out;
}

bool is_ascii(std::u32string_view s) noexcept {
    for (char32_t c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

} // namespace fzfmatch
