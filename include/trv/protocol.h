#pragma once

#include "trv/voice_settings.h"

#include <cstdint>
#include <optional>
#include <string>

namespace trv {

enum class CommandType { Start, Append, Finish, Interrupt, Shutdown };

struct Command {
    CommandType type;
    std::string session;
    std::string text;
    std::optional<std::int64_t> seq;
    VoiceSettingsPatch voice;
};

struct ParseResult {
    std::optional<Command> command;
    std::string code;
    std::string message;
};

constexpr std::size_t kMaximumProtocolLineBytes = 1024 * 1024;

[[nodiscard]] ParseResult parse_command(const std::string& line);
[[nodiscard]] std::string json_event(const std::string& type,
                                     const std::string& session = {},
                                     const std::string& code = {},
                                     const std::string& message = {},
                                     bool recoverable = false,
                                     std::optional<std::int64_t> seq = std::nullopt,
                                     std::optional<std::size_t> bytes = std::nullopt,
                                     const VoiceSettings* voice = nullptr);

}  // namespace trv
