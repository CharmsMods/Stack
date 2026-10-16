#pragma once

#include <string>

namespace Stack::Utf8Text {

inline void TruncateToByteLimit(std::string& text, std::size_t limit) {
    if (text.size() <= limit) return;
    while (limit > 0 && (static_cast<unsigned char>(text[limit]) & 0xC0u) == 0x80u)
        --limit;
    text.resize(limit);
}

inline void RemoveLastCodePoint(std::string& text) {
    if (text.empty()) return;

    std::size_t start = text.size() - 1;
    while (start > 0 &&
        (static_cast<unsigned char>(text[start]) & 0xC0u) == 0x80u) {
        --start;
    }
    text.erase(start);
}

} // namespace Stack::Utf8Text
