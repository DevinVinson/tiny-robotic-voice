#include "trv/text_processing.h"

#include <cctype>
#include <string_view>

namespace trv {
namespace {

std::string trim(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

bool is_terminal(char c) {
    return c == '.' || c == '!' || c == '?';
}

}  // namespace

std::string normalize_text(const std::string& text) {
    std::string result = trim(text);
    if (result.empty()) {
        return result;
    }
    // Capitalize the first alphabetic character.
    result[0] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(result[0])));
    // Add terminal punctuation if the last non-space character is not a
    // sentence terminator. This helps the speech engine produce proper
    // sentence-final prosody (pitch drop, final lengthening).
    if (!is_terminal(result.back())) {
        result += '.';
    }
    return result;
}

}  // namespace trv
