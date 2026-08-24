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
    const double inv_max = 1.0 / 32768.0;
    for (auto& sample : audio.samples) {
        const double normalized = static_cast<double>(sample) * inv_max;
        const double scaled = normalized * gain;
        // Soft clip via tanh: smooth limiting that reduces harshness at high
        // gain without the abrupt onset of hard int16 saturation.
        const double shaped = std::tanh(scaled);
        sample = static_cast<std::int16_t>(
            std::clamp<std::int64_t>(std::llround(shaped * 32767.0), -32768, 32767));
    }
}

void apply_fade_ramps(PcmAudio& audio) {
    if (audio.sample_rate <= 0 || audio.samples.empty()) {
        return;
    }
    // ~5 ms ramp on each side, proportional for very short chunks.
    const std::size_t ramp = static_cast<std::size_t>(audio.sample_rate) * 5 / 1000;
    if (ramp == 0) {
        return;
    }
    const std::size_t frame_size = static_cast<std::size_t>(audio.channels);
    if (frame_size == 0) {
        return;
    }
    const std::size_t ramp_frames = std::min(ramp, audio.frame_count() / 2);
    if (ramp_frames == 0) {
        return;
    }
    const std::size_t ramp_samples = ramp_frames * frame_size;

    // Linear fade-in.
    for (std::size_t i = 0; i < ramp_samples; ++i) {
        const double factor = static_cast<double>(i / frame_size) /
                              static_cast<double>(ramp_frames);
        audio.samples[i] = static_cast<std::int16_t>(
            std::llround(static_cast<double>(audio.samples[i]) * factor));
    }

    // Linear fade-out.
    const std::size_t total = audio.samples.size();
    for (std::size_t i = 0; i < ramp_samples; ++i) {
        const double factor = static_cast<double>(i / frame_size) /
                              static_cast<double>(ramp_frames);
        audio.samples[total - 1 - i] = static_cast<std::int16_t>(
            std::llround(static_cast<double>(audio.samples[total - 1 - i]) * factor));
    }
}

}  // namespace trv
