#pragma once

#include "trv/audio.h"
#include "trv/voice_settings.h"

#include <cstddef>

namespace trv {

void apply_output_gain(PcmAudio& audio, const VoiceSettings& settings);

// Applies short linear fade-in at the start and fade-out at the end of a PCM
// chunk to eliminate discontinuity clicks at boundaries between independently
// synthesized chunks. The ramp duration is ~5 ms, computed from the audio's
// sample rate. Chords shorter than twice the ramp are faded proportionally.
void apply_fade_ramps(PcmAudio& audio);

}  // namespace trv
