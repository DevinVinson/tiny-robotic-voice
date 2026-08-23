#pragma once

#include "trv/audio.h"
#include "trv/voice_settings.h"

namespace trv {

void apply_output_gain(PcmAudio& audio, const VoiceSettings& settings);

}  // namespace trv
