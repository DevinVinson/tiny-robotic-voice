#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace trv {

struct VoiceSettings {
    std::string preset = "default";
    double speed = 1.0;
    double pitch_semitones = 0.0;
    double expression = 1.0;
    double gain_db = 0.0;
};

struct VoiceSettingsPatch {
    std::optional<std::string> preset;
    std::optional<double> speed;
    std::optional<double> pitch_semitones;
    std::optional<double> expression;
    std::optional<double> gain_db;

    [[nodiscard]] bool empty() const;
};

struct VoiceSettingsParseResult {
    std::optional<VoiceSettingsPatch> settings;
    std::string message;
};

constexpr double kMinimumSpeed = 0.6;
constexpr double kMaximumSpeed = 1.8;
constexpr double kMinimumPitchSemitones = -12.0;
constexpr double kMaximumPitchSemitones = 12.0;
constexpr double kMinimumExpression = 0.0;
constexpr double kMaximumExpression = 2.0;
constexpr double kMinimumGainDb = -24.0;
constexpr double kMaximumGainDb = 6.0;
constexpr std::size_t kMaximumVoiceConfigBytes = 64 * 1024;

[[nodiscard]] bool apply_voice_settings_patch(VoiceSettings& settings,
                                              const VoiceSettingsPatch& patch,
                                              std::string& error);
[[nodiscard]] VoiceSettingsParseResult parse_voice_settings_json(const std::string& json);

}  // namespace trv
