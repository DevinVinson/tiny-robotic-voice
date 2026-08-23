#include "trv/speech_engine.h"

#include <iostream>

int main() {
    auto engine = trv::make_flite_engine();
    const auto first = engine->synthesize("Tiny Robotic Voice test one.");
    const auto second = engine->synthesize("Tiny Robotic Voice test two.");
    if (engine->sample_rate() <= 0 || first.samples.empty() || second.samples.empty() ||
        first.channels != 1 || second.channels != 1) {
        std::cerr << "real Flite synthesis test failed\n";
        return 1;
    }
    std::cout << "Flite tests passed (" << first.sample_rate << " Hz, "
              << first.frame_count() << " frames)\n";
    return 0;
}
