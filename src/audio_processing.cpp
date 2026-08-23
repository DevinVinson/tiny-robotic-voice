#include "trv/audio_processing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace trv {

void apply_output_gain(PcmAudio& audio, const VoiceSettings& settings) {
    if (settings.gain_db == 0.0) {
        return;
    }
    const double gain = std::pow(10.0, settings.gain_db / 20.0);
    for (auto& sample : audio.samples) {
        const auto scaled = static_cast<std::int64_t>(std::llround(sample * gain));
        sample = static_cast<std::int16_t>(
            std::clamp<std::int64_t>(scaled, -32768, 32767));
    }
}

}  // namespace trv
