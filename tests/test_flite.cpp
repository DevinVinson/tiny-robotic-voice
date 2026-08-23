#include "trv/speech_engine.h"

#include <iostream>

int main() {
    auto engine = trv::make_flite_engine();
    const trv::VoiceSettings defaults;
    auto fast = defaults;
    fast.speed = 1.5;
    auto high = defaults;
    high.pitch_semitones = 6.0;
    const std::string text = "Tiny Robotic Voice tests configurable speech.";
    const auto first = engine->synthesize(text, defaults);
    const auto second = engine->synthesize(text, fast);
    const auto pitched = engine->synthesize(text, high);
    if (engine->sample_rate() <= 0 || first.samples.empty() || second.samples.empty() ||
        pitched.samples.empty() || first.channels != 1 || second.channels != 1 ||
        second.frame_count() >= first.frame_count() || pitched.samples == first.samples) {
        std::cerr << "real Flite synthesis test failed\n";
        return 1;
    }
    std::cout << "Flite tests passed (" << first.sample_rate << " Hz, "
              << first.frame_count() << " frames)\n";
    return 0;
}
