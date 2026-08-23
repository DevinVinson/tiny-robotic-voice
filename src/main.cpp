#include "trv/audio_processing.h"
#include "trv/miniaudio_output.h"
#include "trv/protocol.h"
#include "trv/speech_engine.h"
#include "trv/streaming_runtime.h"
#include "trv/voice_settings.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kExitUsage = 2;
constexpr int kExitUnsupported = 3;
constexpr int kExitAudioUnavailable = 4;
constexpr int kExitInternal = 5;
constexpr int kExitConfiguration = 6;

volatile std::sig_atomic_t signal_requested = 0;
int signal_pipe_write = -1;

void signal_handler(int) {
    signal_requested = 1;
    if (signal_pipe_write >= 0) {
        const unsigned char byte = 1;
        (void)::write(signal_pipe_write, &byte, sizeof(byte));
    }
}

void print_usage() {
    std::cerr << "Usage:\n"
              << "  trv doctor [--json]\n"
              << "  trv say [voice options] <text>\n"
              << "  trv stream [voice options]\n\n"
              << "Voice options:\n"
              << "  --preset default|tiny|deep|flat\n"
              << "  --speed 0.6..1.8\n"
              << "  --pitch-semitones -12..12\n"
              << "  --expression 0..2\n"
              << "  --gain-db -24..6\n"
              << "  --config <path> | --no-config\n";
}

struct VoiceCliOptions {
    trv::VoiceSettingsPatch patch;
    std::optional<std::string> config_path;
    bool no_config = false;
    std::vector<std::string> positional;
    std::string error;
};

bool parse_number(const std::string& text, double& output) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    output = std::strtod(text.c_str(), &end);
    return errno == 0 && end == text.c_str() + text.size() && std::isfinite(output);
}

VoiceCliOptions parse_voice_cli(int argc, char** argv, int first) {
    VoiceCliOptions options;
    bool positional_only = false;
    for (int index = first; index < argc; ++index) {
        const std::string argument = argv[index];
        if (positional_only || argument.empty() || argument[0] != '-') {
            options.positional.push_back(argument);
            continue;
        }
        if (argument == "--") {
            positional_only = true;
            continue;
        }
        if (argument == "--no-config") {
            options.no_config = true;
            continue;
        }
        auto value = [&]() -> std::optional<std::string> {
            if (index + 1 >= argc) {
                options.error = "Option '" + argument + "' requires a value.";
                return std::nullopt;
            }
            return std::string(argv[++index]);
        };
        if (argument == "--config") {
            const auto parsed = value();
            if (!parsed) break;
            options.config_path = *parsed;
        } else if (argument == "--preset") {
            const auto parsed = value();
            if (!parsed) break;
            options.patch.preset = *parsed;
        } else if (argument == "--speed" || argument == "--pitch-semitones" ||
                   argument == "--expression" || argument == "--gain-db") {
            const auto parsed = value();
            if (!parsed) break;
            double number = 0.0;
            if (!parse_number(*parsed, number)) {
                options.error = "Option '" + argument + "' requires a finite number.";
                break;
            }
            if (argument == "--speed") options.patch.speed = number;
            if (argument == "--pitch-semitones") options.patch.pitch_semitones = number;
            if (argument == "--expression") options.patch.expression = number;
            if (argument == "--gain-db") options.patch.gain_db = number;
        } else {
            options.error = "Unknown option: " + argument;
            break;
        }
    }
    if (options.no_config && options.config_path) {
        options.error = "--config and --no-config cannot be used together.";
    }
    return options;
}

bool load_voice_settings(const VoiceCliOptions& options, trv::VoiceSettings& settings,
                         std::string& error) {
    std::optional<std::filesystem::path> path;
    if (options.config_path) {
        path = *options.config_path;
    } else if (!options.no_config) {
        const std::filesystem::path repository_config = ".trv.json";
        std::error_code status_error;
        if (std::filesystem::exists(repository_config, status_error)) {
            path = repository_config;
        } else if (status_error) {
            error = "Could not inspect .trv.json: " + status_error.message();
            return false;
        }
    }

    if (path) {
        std::error_code size_error;
        const auto size = std::filesystem::file_size(*path, size_error);
        if (size_error) {
            error = "Could not read voice configuration '" + path->string() + "'.";
            return false;
        }
        if (size > trv::kMaximumVoiceConfigBytes) {
            error = "Voice configuration exceeds the 64 KiB limit.";
            return false;
        }
        std::ifstream file(*path, std::ios::binary);
        if (!file) {
            error = "Could not read voice configuration '" + path->string() + "'.";
            return false;
        }
        const std::string json((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        const auto parsed = trv::parse_voice_settings_json(json);
        if (!parsed.settings) {
            error = path->string() + ": " + parsed.message;
            return false;
        }
        if (!trv::apply_voice_settings_patch(settings, *parsed.settings, error)) {
            return false;
        }
    }
    return trv::apply_voice_settings_patch(settings, options.patch, error);
}

bool supported_platform() {
#if defined(__APPLE__) && defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

int doctor(bool structured) {
    if (!supported_platform()) {
        if (structured) {
            std::cout << R"({"status":"unsupported_environment","supported":false})"
                      << std::endl;
        } else {
            std::cout << "Tiny Robotic Voice\nStatus: unsupported\n"
                      << "Reason: macOS Apple Silicon is required\n";
        }
        return kExitUnsupported;
    }

    try {
        auto speech = trv::make_flite_engine();
        trv::MiniaudioOutput audio;
        std::string error;
        if (!audio.initialize(speech->sample_rate(), error)) {
            if (structured) {
                std::cout << trv::json_event("doctor", {}, "audio_unavailable", error, false)
                          << std::endl;
            } else {
                std::cout << "Tiny Robotic Voice\nPlatform: macOS arm64\n"
                          << "Speech engine: Flite\nVoice: cmu_us_kal16\n"
                          << "Status: unsupported\nReason: " << error << '\n';
            }
            return kExitAudioUnavailable;
        }
        audio.stop();
        if (structured) {
            std::cout << R"({"type":"doctor","status":"supported","supported":true,"platform":"macOS arm64","speech_engine":"Flite","voice":"cmu_us_kal16","audio_backend":"Core Audio","output_device":"available"})"
                      << std::endl;
        } else {
            std::cout << "Tiny Robotic Voice\nPlatform: macOS arm64\n"
                      << "Speech engine: Flite\nVoice: cmu_us_kal16\n"
                      << "Audio backend: Core Audio\nOutput device: available\nStatus: ready\n";
        }
        return 0;
    } catch (const std::exception& exception) {
        if (structured) {
            std::cout << trv::json_event("doctor", {}, "internal_error", exception.what(), false)
                      << std::endl;
        } else {
            std::cout << "Tiny Robotic Voice\nStatus: internal error\nReason: "
                      << exception.what() << '\n';
        }
        return kExitInternal;
    }
}

int say(const std::string& text, const trv::VoiceSettings& settings) {
    if (!supported_platform()) {
        std::cerr << "trv: macOS Apple Silicon is required.\n";
        return kExitUnsupported;
    }
    if (text.empty()) {
        std::cerr << "trv: text must not be empty.\n";
        return kExitUsage;
    }
    try {
        auto speech = trv::make_flite_engine();
        trv::MiniaudioOutput audio;
        std::string error;
        if (!audio.initialize(speech->sample_rate(), error)) {
            std::cerr << "trv: " << error << '\n';
            return kExitAudioUnavailable;
        }
        constexpr std::uint64_t generation = 1;
        audio.set_valid_generation(generation);
        trv::PcmAudio pcm = speech->synthesize(text, settings);
        trv::apply_output_gain(pcm, settings);
        if (pcm.samples.empty() || !audio.enqueue(generation, std::move(pcm))) {
            std::cerr << "trv: speech could not be queued for playback.\n";
            return kExitInternal;
        }
        while (!audio.is_drained(generation)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Let frames already handed to Core Audio leave the hardware buffer.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        audio.stop();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "trv: " << exception.what() << '\n';
        return kExitInternal;
    }
}

int stream(const trv::VoiceSettings& settings) {
    if (!supported_platform()) {
        std::cout << trv::json_event("error", {}, "unsupported_environment",
                                    "macOS Apple Silicon is required.", false)
                  << std::endl;
        return kExitUnsupported;
    }
    try {
        auto speech = trv::make_flite_engine();
        trv::MiniaudioOutput audio;
        std::string audio_error;
        if (!audio.initialize(speech->sample_rate(), audio_error)) {
            std::cout << trv::json_event("error", {}, "audio_unavailable", audio_error, false)
                      << std::endl;
            return kExitAudioUnavailable;
        }

        int signal_pipe[2] = {-1, -1};
        if (::pipe(signal_pipe) != 0) {
            throw std::runtime_error("Unable to create the signal notification pipe.");
        }
        signal_pipe_write = signal_pipe[1];
        const int current_flags = ::fcntl(signal_pipe_write, F_GETFL, 0);
        if (current_flags >= 0) {
            (void)::fcntl(signal_pipe_write, F_SETFL, current_flags | O_NONBLOCK);
        }

        std::mutex output_mutex;
        auto emit = [&output_mutex](const std::string& event) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cout << event << std::endl;
        };
        trv::StreamingRuntime runtime(*speech, audio, emit, settings);
        runtime.start_workers();
        emit(trv::json_event("ready", {}, {}, {}, false, std::nullopt,
                             std::nullopt, &settings));

        bool continue_reading = true;
        bool discarding_oversized_line = false;
        std::string pending;
        auto handle_line = [&](const std::string& line) {
            const auto parsed = trv::parse_command(line);
            if (!parsed.command) {
                emit(trv::json_event("error", {}, parsed.code, parsed.message, true));
                return true;
            }
            return runtime.handle(*parsed.command);
        };

        while (continue_reading && !signal_requested) {
            pollfd descriptors[2] = {
                {STDIN_FILENO, POLLIN, 0},
                {signal_pipe[0], POLLIN, 0},
            };
            const int poll_result = ::poll(descriptors, 2, -1);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("Unable to read the protocol stream.");
            }
            if ((descriptors[1].revents & POLLIN) != 0 || signal_requested) {
                break;
            }
            if ((descriptors[0].revents & (POLLIN | POLLHUP)) == 0) {
                continue;
            }

            char input[4096];
            const ssize_t count = ::read(STDIN_FILENO, input, sizeof(input));
            if (count == 0) {
                if (!pending.empty() && !discarding_oversized_line) {
                    continue_reading = handle_line(pending);
                }
                break;
            }
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("Unable to read the protocol stream.");
            }
            for (ssize_t i = 0; i < count && continue_reading; ++i) {
                const char c = input[i];
                if (c == '\n') {
                    if (discarding_oversized_line) {
                        discarding_oversized_line = false;
                        emit(trv::json_event("error", {}, "line_too_large",
                                             "Protocol line exceeds the 1 MiB limit.", true));
                    } else {
                        continue_reading = handle_line(pending);
                    }
                    pending.clear();
                } else if (!discarding_oversized_line) {
                    pending.push_back(c);
                    if (pending.size() > trv::kMaximumProtocolLineBytes) {
                        pending.clear();
                        discarding_oversized_line = true;
                    }
                }
            }
        }
        runtime.shutdown(true);
        audio.stop();
        signal_pipe_write = -1;
        ::close(signal_pipe[0]);
        ::close(signal_pipe[1]);
        return 0;
    } catch (const std::exception& exception) {
        std::cout << trv::json_event("error", {}, "internal_error", exception.what(), false)
                  << std::endl;
        return kExitInternal;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (argc < 2) {
        print_usage();
        return kExitUsage;
    }
    const std::string command = argv[1];
    if (command == "doctor" && (argc == 2 || (argc == 3 && std::string(argv[2]) == "--json"))) {
        return doctor(argc == 3);
    }
    if (command == "say" || command == "stream") {
        const auto options = parse_voice_cli(argc, argv, 2);
        const bool valid_positionals =
            command == "say" ? options.positional.size() == 1 : options.positional.empty();
        if (!options.error.empty() || !valid_positionals) {
            if (!options.error.empty()) std::cerr << "trv: " << options.error << '\n';
            print_usage();
            return kExitUsage;
        }
        trv::VoiceSettings settings;
        std::string error;
        if (!load_voice_settings(options, settings, error)) {
            std::cerr << "trv: " << error << '\n';
            return kExitConfiguration;
        }
        if (command == "say") {
            return say(options.positional.front(), settings);
        }
        return stream(settings);
    }
    print_usage();
    return kExitUsage;
}
