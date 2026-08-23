#pragma once

#include "trv/audio.h"
#include "trv/protocol.h"
#include "trv/speech_engine.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace trv {

class StreamingRuntime {
public:
    using EventSink = std::function<void(const std::string&)>;
    static constexpr std::size_t kMaximumPendingTextBytes = 64 * 1024;

    StreamingRuntime(SpeechEngine& speech, AudioSink& audio, EventSink events,
                     VoiceSettings default_voice = {});
    ~StreamingRuntime();
    StreamingRuntime(const StreamingRuntime&) = delete;
    StreamingRuntime& operator=(const StreamingRuntime&) = delete;

    void start_workers();
    // Returns false when the command requests process shutdown.
    bool handle(const Command& command);
    void shutdown(bool emit_event = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace trv
