// SPDX-License-Identifier: GPL-3.0-or-later
#include "text.hpp"

#include <cstdint>

namespace ren {
namespace {

bool separator(std::uint32_t c) {
    return c <= 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0xa0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
           c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

} // namespace

std::string normalize_dictation(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    bool space = false;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        std::uint32_t code = first;
        std::size_t length = 1;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            code = first & 0x1f;
            length = 2;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            code = first & 0x0f;
            length = 3;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            code = first & 0x07;
            length = 4;
            minimum = 0x10000;
        } else if (first >= 0x80) {
            ++i;
            space = !result.empty();
            continue;
        }

        bool valid = i + length <= text.size();
        for (std::size_t j = 1; valid && j < length; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            valid = (byte & 0xc0) == 0x80;
            code = (code << 6) | (byte & 0x3f);
        }
        valid = valid && code >= minimum && code <= 0x10ffff &&
                !(code >= 0xd800 && code <= 0xdfff);
        if (!valid) {
            ++i;
            space = !result.empty();
            continue;
        }

        if (separator(code)) {
            space = !result.empty();
        } else if (code != 0xfeff) { // A BOM is not dictation text.
            if (space) result += ' ';
            result.append(text.substr(i, length));
            space = false;
        }
        i += length;
    }
    return result;
}

std::string normalize_vocabulary(std::string_view text) {
    auto result = normalize_dictation(text.substr(0, 4096));
    std::size_t characters = 0;
    for (std::size_t i = 0; i < result.size(); ++i) {
        if ((static_cast<unsigned char>(result[i]) & 0xc0) != 0x80 && ++characters > 512) {
            result.resize(i);
            break;
        }
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

} // namespace ren
