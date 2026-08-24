#include "trv/streaming_runtime.h"

#include "trv/audio_processing.h"
#include "trv/chunker.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace trv {
namespace {

using Clock = std::chrono::steady_clock;

struct SynthesisJob {
    std::uint64_t generation;
    std::string session;
    std::string text;
    VoiceSettings voice;
};

class JobQueue {
public:
    struct Cancelled {
        std::size_t jobs = 0;
        std::size_t bytes = 0;
    };

    bool push(SynthesisJob job) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || bytes_ + job.text.size() > StreamingRuntime::kMaximumPendingTextBytes) {
            return false;
        }
        bytes_ += job.text.size();
        jobs_.push_back(std::move(job));
        condition_.notify_one();
        return true;
    }

    bool pop(SynthesisJob& job) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return stopped_ || !jobs_.empty(); });
        if (jobs_.empty()) {
            return false;
        }
        job = std::move(jobs_.front());
        jobs_.pop_front();
        bytes_ -= job.text.size();
        return true;
    }

    Cancelled cancel(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        Cancelled removed;
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->generation == generation) {
                removed.bytes += it->text.size();
                ++removed.jobs;
                it = jobs_.erase(it);
            } else {
                ++it;
            }
        }
        bytes_ -= removed.bytes;
        return removed;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<SynthesisJob> jobs_;
    std::size_t bytes_ = 0;
    bool stopped_ = false;
};

}  // namespace

struct StreamingRuntime::Impl {
    SpeechEngine& speech;
    AudioSink& audio;
    EventSink emit;
    JobQueue jobs;
    std::thread synthesis_thread;
    std::thread maintenance_thread;
    std::mutex state_mutex;
    std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> current_generation{0};
    std::atomic<std::size_t> outstanding_jobs{0};
    std::atomic<std::size_t> owned_text_bytes{0};
    bool workers_started = false;
    bool active = false;
    bool accepting = false;
    bool speaking_emitted = false;
    VoiceSettings default_voice;
    VoiceSettings current_voice;
    std::uint64_t generation = 0;
    std::string session;
    std::string buffer;
    Clock::time_point last_append = Clock::now();

    Impl(SpeechEngine& speech_engine, AudioSink& audio_sink, EventSink events,
         VoiceSettings voice)
        : speech(speech_engine), audio(audio_sink), emit(std::move(events)),
          default_voice(std::move(voice)), current_voice(default_voice) {}

    void error(const std::string& code, const std::string& message,
               bool recoverable = true, const std::string& error_session = {}) {
        emit(json_event("error", error_session, code, message, recoverable));
    }

    bool commit_one_locked(bool force) {
        const auto bytes = PhraseChunker::next_chunk_bytes(buffer, force);
        if (bytes == 0) {
            return false;
        }
        std::string phrase = buffer.substr(0, bytes);
        if (!PhraseChunker::has_speakable_text(phrase)) {
            buffer.erase(0, bytes);
            owned_text_bytes.fetch_sub(bytes, std::memory_order_relaxed);
            return true;
        }
        if (!jobs.push({generation, session, phrase, current_voice})) {
            return false;
        }
        outstanding_jobs.fetch_add(1, std::memory_order_release);
        buffer.erase(0, bytes);
        return true;
    }

    void commit_available_locked(bool force) {
        while (!buffer.empty()) {
            const auto before = buffer.size();
            if (!commit_one_locked(force)) {
                break;
            }
            if (!force && buffer.size() == before) {
                break;
            }
        }
    }

    void synthesis_loop() {
        SynthesisJob job;
        while (jobs.pop(job)) {
            const bool valid_before =
                job.generation == current_generation.load(std::memory_order_acquire);
            if (valid_before) {
                try {
                    PcmAudio pcm = speech.synthesize(job.text, job.voice);
                    apply_fade_ramps(pcm);
                    apply_output_gain(pcm, job.voice);
                    if (job.generation == current_generation.load(std::memory_order_acquire) &&
                        !pcm.samples.empty()) {
                        if (audio.enqueue(job.generation, std::move(pcm))) {
                            std::lock_guard<std::mutex> lock(state_mutex);
                            if (active && generation == job.generation && !speaking_emitted) {
                                speaking_emitted = true;
                                emit(json_event("speaking", session));
                            }
                        }
                    }
                } catch (const std::exception& exception) {
                    if (job.generation == current_generation.load(std::memory_order_acquire)) {
                        error("synthesis_failed", exception.what(), false, job.session);
                    }
                }
            }
            owned_text_bytes.fetch_sub(job.text.size(), std::memory_order_relaxed);
            outstanding_jobs.fetch_sub(1, std::memory_order_release);
        }
    }

    void maintenance_loop() {
        while (!stopping.load(std::memory_order_acquire)) {
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (active && accepting && !buffer.empty() &&
                    Clock::now() - last_append >= std::chrono::milliseconds(400)) {
                    commit_available_locked(true);
                }
                if (active && !accepting) {
                    commit_available_locked(true);
                    if (buffer.empty() &&
                        outstanding_jobs.load(std::memory_order_acquire) == 0 &&
                        audio.is_drained(generation)) {
                        const std::string completed_session = session;
                        active = false;
                        session.clear();
                        emit(json_event("finished", completed_session));
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
};

StreamingRuntime::StreamingRuntime(SpeechEngine& speech, AudioSink& audio, EventSink events,
                                   VoiceSettings default_voice)
    : impl_(std::make_unique<Impl>(speech, audio, std::move(events),
                                   std::move(default_voice))) {}

StreamingRuntime::~StreamingRuntime() { shutdown(false); }

void StreamingRuntime::start_workers() {
    if (impl_->workers_started) {
        return;
    }
    impl_->workers_started = true;
    impl_->synthesis_thread = std::thread([this] { impl_->synthesis_loop(); });
    impl_->maintenance_thread = std::thread([this] { impl_->maintenance_loop(); });
}

bool StreamingRuntime::handle(const Command& command) {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    switch (command.type) {
        case CommandType::Start: {
            if (impl_->active) {
                impl_->error("invalid_state", "A speaking session is already active.", true,
                             command.session);
                return true;
            }
            impl_->current_voice = impl_->default_voice;
            std::string settings_error;
            if (!apply_voice_settings_patch(impl_->current_voice, command.voice,
                                            settings_error)) {
                impl_->error("invalid_voice_settings", settings_error, true,
                             command.session);
                return true;
            }
            impl_->generation = impl_->current_generation.fetch_add(1) + 1;
            impl_->audio.set_valid_generation(impl_->generation);
            impl_->active = true;
            impl_->accepting = true;
            impl_->speaking_emitted = false;
            impl_->session = command.session;
            impl_->buffer.clear();
            impl_->last_append = Clock::now();
            impl_->emit(json_event("session_started", impl_->session, {}, {}, false,
                                   std::nullopt, std::nullopt, &impl_->current_voice));
            return true;
        }
        case CommandType::Append: {
            if (!impl_->active || !impl_->accepting || command.session != impl_->session) {
                impl_->error("invalid_state", "Append does not match an active receiving session.",
                             true, command.session);
                return true;
            }
            if (command.text.size() > kMaximumPendingTextBytes -
                                        std::min(kMaximumPendingTextBytes,
                                                 impl_->owned_text_bytes.load())) {
                impl_->emit(json_event("error", command.session, "backpressure",
                                       "Pending speech capacity is exhausted.", true,
                                       command.seq));
                return true;
            }
            impl_->owned_text_bytes.fetch_add(command.text.size(), std::memory_order_relaxed);
            impl_->buffer += command.text;
            impl_->last_append = Clock::now();
            impl_->commit_available_locked(false);
            impl_->emit(json_event("accepted", impl_->session, {}, {}, false,
                                   command.seq, command.text.size()));
            return true;
        }
        case CommandType::Finish: {
            if (!impl_->active || !impl_->accepting || command.session != impl_->session) {
                impl_->error("invalid_state", "Finish does not match an active receiving session.",
                             true, command.session);
                return true;
            }
            impl_->accepting = false;
            impl_->commit_available_locked(true);
            return true;
        }
        case CommandType::Interrupt: {
            if (!impl_->active || command.session != impl_->session) {
                impl_->error("invalid_state", "Interrupt does not match an active session.", true,
                             command.session);
                return true;
            }
            const auto interrupted_generation = impl_->generation;
            const auto interrupted_session = impl_->session;
            impl_->current_generation.fetch_add(1, std::memory_order_acq_rel);
            impl_->audio.set_valid_generation(impl_->current_generation.load());
            impl_->owned_text_bytes.fetch_sub(impl_->buffer.size(), std::memory_order_relaxed);
            impl_->buffer.clear();
            const auto removed = impl_->jobs.cancel(interrupted_generation);
            impl_->owned_text_bytes.fetch_sub(removed.bytes, std::memory_order_relaxed);
            impl_->outstanding_jobs.fetch_sub(removed.jobs, std::memory_order_release);
            impl_->audio.interrupt();
            impl_->active = false;
            impl_->accepting = false;
            impl_->session.clear();
            impl_->emit(json_event("interrupted", interrupted_session));
            return true;
        }
        case CommandType::Shutdown:
            break;
    }
    return false;
}

void StreamingRuntime::shutdown(bool emit_event) {
    if (!impl_ || impl_->stopping.exchange(true)) {
        return;
    }
    if (emit_event) {
        impl_->emit(json_event("shutting_down"));
    }
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        impl_->current_generation.fetch_add(1, std::memory_order_acq_rel);
        impl_->audio.set_valid_generation(impl_->current_generation.load());
        impl_->audio.interrupt();
        impl_->active = false;
        impl_->accepting = false;
        impl_->buffer.clear();
    }
    impl_->jobs.stop();
    if (impl_->synthesis_thread.joinable()) {
        impl_->synthesis_thread.join();
    }
    if (impl_->maintenance_thread.joinable()) {
        impl_->maintenance_thread.join();
    }
}

}  // namespace trv
