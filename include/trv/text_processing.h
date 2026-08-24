#pragma once

#include <string>

namespace trv {

// Normalizes text for synthesis: trims whitespace, capitalizes the first
// letter, and adds terminal punctuation if missing. Improves prosody by
// giving the speech engine well-formed sentence boundaries.
[[nodiscard]] std::string normalize_text(const std::string& text);

}  // namespace trv
