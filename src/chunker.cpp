#include "trv/chunker.h"

#include <algorithm>
#include <cctype>

namespace trv {
namespace {

bool ascii_space(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::size_t include_following_space(const std::string& text, std::size_t end) {
    while (end < text.size() && ascii_space(text[end])) {
        ++end;
    }
    return end;
}

}  // namespace

bool PhraseChunker::has_speakable_text(const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](char c) { return !ascii_space(c); });
}

std::size_t PhraseChunker::next_chunk_bytes(const std::string& buffer, bool force) {
    if (buffer.empty()) {
        return 0;
    }

    // Strong sentence boundaries are committed as soon as the phrase is useful.
    for (std::size_t i = kMinimumChunk - 1; i < buffer.size(); ++i) {
        const char c = buffer[i];
        if ((c == '.' || c == '!' || c == '?') &&
            (i + 1 == buffer.size() || ascii_space(buffer[i + 1]))) {
            return include_following_space(buffer, i + 1);
        }
    }

    const auto newline = buffer.find('\n');
    if (newline != std::string::npos && newline + 1 >= kMinimumChunk) {
        return newline + 1;
    }

    if (buffer.size() >= kPreferredChunk) {
        const std::size_t limit = std::min(buffer.size(), kPreferredChunk);
        for (std::size_t i = limit; i-- > kMinimumChunk;) {
            if (buffer[i] == ',' || buffer[i] == ';' || buffer[i] == ':') {
                return include_following_space(buffer, i + 1);
            }
        }
        for (std::size_t i = limit; i-- > kMinimumChunk;) {
            if (ascii_space(buffer[i])) {
                return include_following_space(buffer, i + 1);
            }
        }
    }

    if (buffer.size() >= kHardMaximum) {
        for (std::size_t i = kHardMaximum; i-- > kMinimumChunk;) {
            if (ascii_space(buffer[i])) {
                return include_following_space(buffer, i + 1);
            }
        }
        return kHardMaximum;
    }

    return force ? buffer.size() : 0;
}

}  // namespace trv
