#include "trv/speech_engine.h"
#include "trv/text_processing.h"

extern "C" {
#include "flite.h"
cst_voice* register_cmu_us_kal16(const char* voxdir);
void unregister_cmu_us_kal16(cst_voice* voice);
}

#include <cmath>
#include <stdexcept>

namespace trv {
namespace {

class FliteEngine final : public SpeechEngine {
public:
    FliteEngine() {
        flite_init();
        voice_ = register_cmu_us_kal16(nullptr);
        if (!voice_) {
            throw std::runtime_error("The bundled cmu_us_kal16 voice could not be initialized.");
        }
        sample_rate_ = feat_int(voice_->features, "sample_rate");
        if (sample_rate_ <= 0) {
            throw std::runtime_error("The bundled voice reported an invalid sample rate.");
        }
    }

    ~FliteEngine() override {
        if (voice_) {
            unregister_cmu_us_kal16(voice_);
        }
    }

    int sample_rate() const override { return sample_rate_; }

    PcmAudio synthesize(const std::string& text,
                        const VoiceSettings& settings) override {
        const std::string normalized = normalize_text(text);
        flite_feat_set_float(voice_->features, "duration_stretch",
                             static_cast<float>(1.1 / settings.speed));
        flite_feat_set_float(
            voice_->features, "f0_shift",
            static_cast<float>(std::pow(2.0, settings.pitch_semitones / 12.0)));
        flite_feat_set_float(voice_->features, "int_f0_target_stddev",
                             static_cast<float>(11.0 * settings.expression));
        cst_wave* wave = flite_text_to_wave(normalized.c_str(), voice_);
        if (!wave) {
            throw std::runtime_error("Flite did not produce a waveform.");
        }
        PcmAudio audio;
        audio.sample_rate = wave->sample_rate;
        audio.channels = wave->num_channels;
        const std::size_t count = static_cast<std::size_t>(wave->num_samples) *
                                  static_cast<std::size_t>(wave->num_channels);
        audio.samples.assign(wave->samples, wave->samples + count);
        delete_wave(wave);
        return audio;
    }

private:
    cst_voice* voice_ = nullptr;
    int sample_rate_ = 0;
};

}  // namespace

std::unique_ptr<SpeechEngine> make_flite_engine() {
    return std::make_unique<FliteEngine>();
}

}  // namespace trv
