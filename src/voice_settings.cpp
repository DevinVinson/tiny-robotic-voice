#include "trv/voice_settings.h"

#include "yyjson.h"

#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

namespace trv {
namespace {

using DocPtr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;

bool known_property(std::string_view key) {
    return key == "model" || key == "preset" || key == "speed" ||
           key == "pitch_semitones" || key == "expression" || key == "gain_db";
}

bool model(const std::string& name) {
    return name == "kal16" || name == "rms";
}

bool number(yyjson_val* object, const char* key, std::optional<double>& output,
            double minimum, double maximum, std::string& error) {
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!value) {
        return true;
    }
    if (!yyjson_is_num(value)) {
        error = std::string("Voice setting '") + key + "' must be a number.";
        return false;
    }
    const double parsed = yyjson_get_num(value);
    if (!std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
        error = std::string("Voice setting '") + key + "' is outside its supported range.";
        return false;
    }
    output = parsed;
    return true;
}

bool preset(const std::string& name, VoiceSettings& settings) {
    if (name == "default") {
        settings.preset = "default";
        settings.speed = 1.0;
        settings.pitch_semitones = 0.0;
        settings.expression = 1.0;
        settings.gain_db = 0.0;
    } else if (name == "tiny") {
        settings.preset = "tiny";
        settings.speed = 1.1;
        settings.pitch_semitones = 4.0;
        settings.expression = 0.7;
        settings.gain_db = -1.0;
    } else if (name == "deep") {
        settings.preset = "deep";
        settings.speed = 0.9;
        settings.pitch_semitones = -4.0;
        settings.expression = 0.8;
        settings.gain_db = 0.0;
    } else if (name == "flat") {
        settings.preset = "flat";
        settings.speed = 1.0;
        settings.pitch_semitones = 0.0;
        settings.expression = 0.15;
        settings.gain_db = 0.0;
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool VoiceSettingsPatch::empty() const {
    return !model && !preset && !speed && !pitch_semitones && !expression && !gain_db;
}

bool apply_voice_settings_patch(VoiceSettings& settings, const VoiceSettingsPatch& patch,
                                std::string& error) {
    if (patch.preset && !preset(*patch.preset, settings)) {
        error = "Unknown voice preset. Expected default, tiny, deep, or flat.";
        return false;
    }
    if (patch.model && !model(*patch.model)) {
        error = "Unknown voice model. Expected kal16 or rms.";
        return false;
    }
    if (patch.model) settings.model = *patch.model;
    if (patch.speed) settings.speed = *patch.speed;
    if (patch.pitch_semitones) settings.pitch_semitones = *patch.pitch_semitones;
    if (patch.expression) settings.expression = *patch.expression;
    if (patch.gain_db) settings.gain_db = *patch.gain_db;

    if (!std::isfinite(settings.speed) || settings.speed < kMinimumSpeed ||
        settings.speed > kMaximumSpeed || !std::isfinite(settings.pitch_semitones) ||
        settings.pitch_semitones < kMinimumPitchSemitones ||
        settings.pitch_semitones > kMaximumPitchSemitones ||
        !std::isfinite(settings.expression) || settings.expression < kMinimumExpression ||
        settings.expression > kMaximumExpression || !std::isfinite(settings.gain_db) ||
        settings.gain_db < kMinimumGainDb || settings.gain_db > kMaximumGainDb) {
        error = "One or more voice settings are outside their supported ranges.";
        return false;
    }
    return true;
}

VoiceSettingsParseResult parse_voice_settings_json(const std::string& json) {
    if (json.size() > kMaximumVoiceConfigBytes) {
        return {std::nullopt, "Voice configuration exceeds the 64 KiB limit."};
    }
    yyjson_read_err read_error{};
    DocPtr doc(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                YYJSON_READ_NOFLAG, nullptr, &read_error),
               &yyjson_doc_free);
    if (!doc) {
        return {std::nullopt, "Voice configuration is not valid JSON."};
    }
    yyjson_val* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return {std::nullopt, "Voice configuration must be a JSON object."};
    }

    std::size_t index = 0;
    std::size_t maximum = 0;
    yyjson_val* key = nullptr;
    yyjson_val* value = nullptr;
    yyjson_obj_foreach(root, index, maximum, key, value) {
        const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        if (!known_property(name)) {
            return {std::nullopt, "Unknown voice setting: " + std::string(name) + "."};
        }
    }

    VoiceSettingsPatch patch;
    if (yyjson_val* model_value = yyjson_obj_get(root, "model")) {
        if (!yyjson_is_str(model_value)) {
            return {std::nullopt, "Voice setting 'model' must be a string."};
        }
        patch.model = std::string(yyjson_get_str(model_value),
                                  yyjson_get_len(model_value));
        if (patch.model->find('\0') != std::string::npos) {
            return {std::nullopt, "Voice setting 'model' contains an invalid character."};
        }
    }
    if (yyjson_val* preset_value = yyjson_obj_get(root, "preset")) {
        if (!yyjson_is_str(preset_value)) {
            return {std::nullopt, "Voice setting 'preset' must be a string."};
        }
        patch.preset = std::string(yyjson_get_str(preset_value),
                                   yyjson_get_len(preset_value));
        if (patch.preset->find('\0') != std::string::npos) {
            return {std::nullopt, "Voice setting 'preset' contains an invalid character."};
        }
    }
    std::string error;
    if (!number(root, "speed", patch.speed, kMinimumSpeed, kMaximumSpeed, error) ||
        !number(root, "pitch_semitones", patch.pitch_semitones,
                kMinimumPitchSemitones, kMaximumPitchSemitones, error) ||
        !number(root, "expression", patch.expression,
                kMinimumExpression, kMaximumExpression, error) ||
        !number(root, "gain_db", patch.gain_db, kMinimumGainDb, kMaximumGainDb, error)) {
        return {std::nullopt, std::move(error)};
    }
    VoiceSettings validation;
    if (!apply_voice_settings_patch(validation, patch, error)) {
        return {std::nullopt, std::move(error)};
    }
    return {std::move(patch), {}};
}

}  // namespace trv
