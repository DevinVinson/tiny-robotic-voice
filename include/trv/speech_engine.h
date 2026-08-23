#pragma once

#include "trv/audio.h"
#include "trv/voice_settings.h"

#include <memory>
#include <string>

namespace trv {

class SpeechEngine {
public:
    virtual ~SpeechEngine() = default;
    [[nodiscard]] virtual int sample_rate() const = 0;
    virtual PcmAudio synthesize(const std::string& text,
                                const VoiceSettings& settings) = 0;
};

std::unique_ptr<SpeechEngine> make_flite_engine();

}  // namespace trv
