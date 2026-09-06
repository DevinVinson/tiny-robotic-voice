#include "trv/speech_engine.h"

extern "C" {
#include "flite.h"
cst_voice* register_cmu_us_kal16(const char* voxdir);
void unregister_cmu_us_kal16(cst_voice* voice);
cst_voice* register_cmu_us_rms(const char* voxdir);
void unregister_cmu_us_rms(cst_voice* voice);
}

#include <cmath>
#include <stdexcept>

namespace trv {
namespace {

class FliteEngine final : public SpeechEngine {
public:
    FliteEngine() {
        flite_init();
        try {
            kal16_voice_ = register_cmu_us_kal16(nullptr);
            if (!kal16_voice_) {
                throw std::runtime_error(
                    "The bundled cmu_us_kal16 voice could not be initialized.");
            }
            rms_voice_ = register_cmu_us_rms(nullptr);
            if (!rms_voice_) {
                throw std::runtime_error(
                    "The bundled cmu_us_rms voice could not be initialized.");
            }
            const int kal16_sample_rate =
                feat_int(kal16_voice_->features, "sample_rate");
            const int rms_sample_rate = feat_int(rms_voice_->features, "sample_rate");
            if (kal16_sample_rate <= 0 || rms_sample_rate <= 0) {
                throw std::runtime_error(
                    "A bundled voice reported an invalid sample rate.");
            }
            if (kal16_sample_rate != rms_sample_rate) {
                throw std::runtime_error(
                    "Bundled voice sample rates do not match.");
            }
            sample_rate_ = kal16_sample_rate;
        } catch (...) {
            unregister_voices();
            throw;
        }
    }

    ~FliteEngine() override { unregister_voices(); }

    int sample_rate() const override { return sample_rate_; }

    PcmAudio synthesize(const std::string& text,
                        const VoiceSettings& settings) override {
        cst_voice* voice = voice_for(settings.model);
        flite_feat_set_float(voice->features, "duration_stretch",
                             static_cast<float>(1.1 / settings.speed));
        flite_feat_set_float(
            voice->features, "f0_shift",
            static_cast<float>(std::pow(2.0, settings.pitch_semitones / 12.0)));
        flite_feat_set_float(voice->features, "int_f0_target_stddev",
                             static_cast<float>(11.0 * settings.expression));
        cst_wave* wave = flite_text_to_wave(text.c_str(), voice);
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
    cst_voice* voice_for(const std::string& model) const {
        if (model == "kal16") return kal16_voice_;
        if (model == "rms") return rms_voice_;
        throw std::runtime_error("Unknown voice model: " + model + ".");
    }

    void unregister_voices() {
        if (rms_voice_) {
            unregister_cmu_us_rms(rms_voice_);
            rms_voice_ = nullptr;
        }
        if (kal16_voice_) {
            unregister_cmu_us_kal16(kal16_voice_);
            kal16_voice_ = nullptr;
        }
    }

    cst_voice* kal16_voice_ = nullptr;
    cst_voice* rms_voice_ = nullptr;
    int sample_rate_ = 0;
};

}  // namespace

std::unique_ptr<SpeechEngine> make_flite_engine() {
    return std::make_unique<FliteEngine>();
}

}  // namespace trv
