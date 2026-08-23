#include "trv/miniaudio_output.h"
#include "trv/protocol.h"
#include "trv/speech_engine.h"
#include "trv/streaming_runtime.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <exception>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

constexpr int kExitUsage = 2;
constexpr int kExitUnsupported = 3;
constexpr int kExitAudioUnavailable = 4;
constexpr int kExitInternal = 5;

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
              << "  trv say <text>\n"
              << "  trv stream\n";
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
                          << "Speech engine: Flite\nVoice: cmu_us_kal\n"
                          << "Status: unsupported\nReason: " << error << '\n';
            }
            return kExitAudioUnavailable;
        }
        audio.stop();
        if (structured) {
            std::cout << R"({"type":"doctor","status":"supported","supported":true,"platform":"macOS arm64","speech_engine":"Flite","voice":"cmu_us_kal","audio_backend":"Core Audio","output_device":"available"})"
                      << std::endl;
        } else {
            std::cout << "Tiny Robotic Voice\nPlatform: macOS arm64\n"
                      << "Speech engine: Flite\nVoice: cmu_us_kal\n"
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

int say(const std::string& text) {
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
        trv::PcmAudio pcm = speech->synthesize(text);
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

int stream() {
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
        trv::StreamingRuntime runtime(*speech, audio, emit);
        runtime.start_workers();
        emit(trv::json_event("ready"));

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
    if (command == "say" && argc == 3) {
        return say(argv[2]);
    }
    if (command == "stream" && argc == 2) {
        return stream();
    }
    print_usage();
    return kExitUsage;
}
