#include "trv/streaming_runtime.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

class BlockingSpeech final : public trv::SpeechEngine {
public:
    int sample_rate() const override { return 8000; }

    trv::PcmAudio synthesize(const std::string& text,
                             const trv::VoiceSettings& settings) override {
        std::unique_lock<std::mutex> lock(mutex);
        synthesized.push_back(text);
        voices.push_back(settings);
        entered = true;
        condition.notify_all();
        if (block_first && synthesized.size() == 1) {
            condition.wait(lock, [this] { return released; });
        }
        trv::PcmAudio result;
        result.sample_rate = 8000;
        result.channels = 1;
        result.samples.assign(80, static_cast<int16_t>(100));
        return result;
    }

    void wait_until_entered() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, 2s, [this] { return entered; });
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        condition.notify_all();
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> synthesized;
    std::vector<trv::VoiceSettings> voices;
    bool block_first = true;
    bool entered = false;
    bool released = false;
};

class RecordingAudio final : public trv::AudioSink {
public:
    bool enqueue(std::uint64_t generation, trv::PcmAudio) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (generation != valid) {
            return false;
        }
        played.push_back(generation);
        return true;
    }

    void set_valid_generation(std::uint64_t generation) override {
        std::lock_guard<std::mutex> lock(mutex);
        valid = generation;
    }

    void interrupt() override {
        std::lock_guard<std::mutex> lock(mutex);
        played.clear();
    }

    bool is_drained(std::uint64_t) const override { return true; }

    mutable std::mutex mutex;
    std::uint64_t valid = 0;
    std::vector<std::uint64_t> played;
};

trv::Command command(trv::CommandType type, std::string session,
                     std::string text = {}) {
    return {type, std::move(session), std::move(text), std::nullopt, {}};
}

bool wait_for_event(std::mutex& mutex, const std::vector<std::string>& events,
                    const std::string& needle) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const auto& event : events) {
                if (event.find(needle) != std::string::npos) {
                    return true;
                }
            }
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

}  // namespace

int main() {
    {
        BlockingSpeech speech;
        RecordingAudio audio;
        std::mutex event_mutex;
        std::vector<std::string> events;
        trv::StreamingRuntime runtime(speech, audio, [&](const std::string& event) {
            std::lock_guard<std::mutex> lock(event_mutex);
            events.push_back(event);
        });
        runtime.start_workers();

        runtime.handle(command(trv::CommandType::Start, "A"));
        runtime.handle(command(
            trv::CommandType::Append, "A",
            "This obsolete sentence is long enough to synthesize immediately."));
        speech.wait_until_entered();
        runtime.handle(command(trv::CommandType::Interrupt, "A"));
        auto replacement = command(trv::CommandType::Start, "B");
        replacement.voice.model = "rms";
        replacement.voice.preset = "deep";
        replacement.voice.speed = 1.2;
        runtime.handle(replacement);
        runtime.handle(command(trv::CommandType::Append, "B",
                               "That response was interrupted. This is the replacement."));
        runtime.handle(command(trv::CommandType::Finish, "B"));
        speech.release();

        const bool interrupted =
            wait_for_event(event_mutex, events, "\"type\":\"interrupted\"");
        const bool finished = wait_for_event(event_mutex, events, "\"type\":\"finished\"");
        bool only_replacement_played = false;
        bool replacement_voice_applied = false;
        {
            std::lock_guard<std::mutex> lock(audio.mutex);
            only_replacement_played = !audio.played.empty();
            for (const auto generation : audio.played) {
                only_replacement_played = only_replacement_played && generation == 3;
            }
        }
        {
            std::lock_guard<std::mutex> lock(speech.mutex);
            replacement_voice_applied = speech.voices.size() >= 2 &&
                speech.voices.back().model == "rms" &&
                speech.voices.back().preset == "deep" &&
                speech.voices.back().speed == 1.2;
        }
        runtime.shutdown(false);
        if (!interrupted || !finished || !only_replacement_played ||
            !replacement_voice_applied) {
            std::cerr << "runtime cancellation/replacement test failed\n";
            return 1;
        }
    }

    {
        BlockingSpeech speech;
        RecordingAudio audio;
        std::mutex event_mutex;
        std::vector<std::string> events;
        trv::StreamingRuntime runtime(speech, audio, [&](const std::string& event) {
            std::lock_guard<std::mutex> lock(event_mutex);
            events.push_back(event);
        });
        runtime.start_workers();
        runtime.handle(command(trv::CommandType::Start, "saturated"));
        runtime.handle(command(trv::CommandType::Append, "saturated",
                               std::string(trv::StreamingRuntime::kMaximumPendingTextBytes, 'a')));
        speech.wait_until_entered();
        runtime.handle(command(trv::CommandType::Append, "saturated", "x"));
        const bool backpressure = wait_for_event(event_mutex, events, "\"code\":\"backpressure\"");
        runtime.handle(command(trv::CommandType::Interrupt, "saturated"));
        const bool interrupt_while_full =
            wait_for_event(event_mutex, events, "\"type\":\"interrupted\"");
        speech.release();
        runtime.shutdown(false);
        if (!backpressure || !interrupt_while_full) {
            std::cerr << "runtime bounded-queue test failed\n";
            return 1;
        }
    }

    {
        BlockingSpeech speech;
        speech.block_first = false;
        RecordingAudio audio;
        std::mutex event_mutex;
        std::vector<std::string> events;
        trv::StreamingRuntime runtime(speech, audio, [&](const std::string& event) {
            std::lock_guard<std::mutex> lock(event_mutex);
            events.push_back(event);
        });
        runtime.start_workers();
        runtime.handle(command(trv::CommandType::Start, "idle"));
        runtime.handle(command(trv::CommandType::Append, "idle", "short idle fragment"));
        const bool idle_spoke = wait_for_event(event_mutex, events, "\"type\":\"speaking\"");
        runtime.handle(command(trv::CommandType::Finish, "idle"));
        const bool idle_finished = wait_for_event(event_mutex, events, "\"type\":\"finished\"");
        runtime.shutdown(false);
        if (!idle_spoke || !idle_finished) {
            std::cerr << "runtime idle-flush test failed\n";
            return 1;
        }
    }

    std::cout << "runtime tests passed\n";
    return 0;
}
