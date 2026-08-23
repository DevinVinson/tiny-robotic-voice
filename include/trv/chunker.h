#pragma once

#include <cstddef>
#include <string>

namespace trv {

class PhraseChunker {
public:
    static constexpr std::size_t kMinimumChunk = 24;
    static constexpr std::size_t kPreferredChunk = 100;
    static constexpr std::size_t kHardMaximum = 180;

    // Returns the number of bytes forming the next phrase, or zero when more
    // producer input should be collected. A force flush returns the remainder.
    [[nodiscard]] static std::size_t next_chunk_bytes(const std::string& buffer,
                                                      bool force);
    [[nodiscard]] static bool has_speakable_text(const std::string& text);
};

}  // namespace trv
