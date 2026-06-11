#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fzfmatch {

// UTF-8 <-> codepoint helpers. Tiny inline decoder, no external deps.
// Invalid sequences emit U+FFFD and advance one byte (best-effort recovery).

std::u32string decode_utf8(std::string_view src);
std::string    encode_utf8(std::u32string_view src);

// Maps each codepoint index in `s` back to its byte offset in `utf8`. Used to
// translate match positions returned by the algorithm (codepoint indices) into
// byte offsets for highlighting.
std::vector<std::size_t> codepoint_to_byte_offsets(std::string_view utf8);

bool is_ascii(std::u32string_view s) noexcept;

} // namespace fzfmatch
