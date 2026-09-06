#include "trv/speech_engine.h"

#include <iostream>

int main() {
    auto engine = trv::make_flite_engine();
    const trv::VoiceSettings defaults;
    auto fast = defaults;
    fast.speed = 1.5;
    auto high = defaults;
    high.pitch_semitones = 6.0;
    auto rms = defaults;
    rms.model = "rms";
    auto rms_fast = rms;
    rms_fast.speed = 1.5;
    auto rms_high = rms;
    rms_high.pitch_semitones = 6.0;
    const std::string text = "Tiny Robotic Voice tests configurable speech.";
    const auto first = engine->synthesize(text, defaults);
    const auto second = engine->synthesize(text, fast);
    const auto pitched = engine->synthesize(text, high);
    const auto cluster = engine->synthesize(text, rms);
    const auto cluster_fast = engine->synthesize(text, rms_fast);
    const auto cluster_pitched = engine->synthesize(text, rms_high);
    const auto kal_after_cluster = engine->synthesize(text, defaults);
    if (engine->sample_rate() <= 0 || first.samples.empty() || second.samples.empty() ||
        pitched.samples.empty() || cluster.samples.empty() || cluster_fast.samples.empty() ||
        cluster_pitched.samples.empty() || kal_after_cluster.samples.empty() ||
        first.channels != 1 || second.channels != 1 || cluster.channels != 1 ||
        first.sample_rate != engine->sample_rate() ||
        cluster.sample_rate != engine->sample_rate() ||
        second.frame_count() >= first.frame_count() || pitched.samples == first.samples ||
        cluster_fast.frame_count() >= cluster.frame_count() ||
        cluster_pitched.samples == cluster.samples || cluster.samples == first.samples) {
        std::cerr << "real Flite synthesis test failed\n";
        return 1;
    }
    std::cout << "Flite tests passed (" << first.sample_rate << " Hz, kal16 "
              << first.frame_count() << " frames, rms " << cluster.frame_count()
              << " frames)\n";
    return 0;
}
