#include "trv/miniaudio_output.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace trv {

struct MiniaudioOutput::Impl {
    struct QueuedAudio {
        std::uint64_t generation;
        PcmAudio audio;
    };

    ma_device device{};
    mutable std::mutex mutex;
    std::deque<QueuedAudio> queue;
    QueuedAudio current{};
    bool has_current = false;
    std::size_t current_frame = 0;
    std::size_t queued_frames = 0;
    std::size_t maximum_frames = 0;
    std::atomic<std::uint64_t> valid_generation{0};
    std::atomic<bool> stopping{false};
    bool initialized = false;

    static void callback(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* destination = static_cast<int16_t*>(output);
        std::memset(destination, 0, sizeof(int16_t) * frame_count);

        // Missing the lock produces a callback-sized patch of silence instead of
        // blocking the real-time audio thread behind producer-side work.
        std::unique_lock<std::mutex> lock(self->mutex, std::try_to_lock);
        if (!lock.owns_lock() || self->stopping.load(std::memory_order_relaxed)) {
            return;
        }

        std::size_t written = 0;
        const auto valid = self->valid_generation.load(std::memory_order_acquire);
        while (written < frame_count) {
            if (self->has_current && self->current.generation != valid) {
                const auto remaining = self->current.audio.frame_count() - self->current_frame;
                self->queued_frames -= std::min(self->queued_frames, remaining);
                self->has_current = false;
                self->current_frame = 0;
            }
            while (!self->has_current && !self->queue.empty()) {
                self->current = std::move(self->queue.front());
                self->queue.pop_front();
                self->current_frame = 0;
                self->has_current = self->current.generation == valid;
                if (!self->has_current) {
                    const auto discarded = self->current.audio.frame_count();
                    self->queued_frames -= std::min(self->queued_frames, discarded);
                }
            }
            if (!self->has_current) {
                break;
            }

            const auto remaining = self->current.audio.frame_count() - self->current_frame;
            const auto count = std::min<std::size_t>(remaining, frame_count - written);
            std::memcpy(destination + written,
                        self->current.audio.samples.data() + self->current_frame,
                        count * sizeof(int16_t));
            written += count;
            self->current_frame += count;
            self->queued_frames -= std::min(self->queued_frames, count);
            if (self->current_frame == self->current.audio.frame_count()) {
                self->has_current = false;
                self->current_frame = 0;
            }
        }
    }
};

MiniaudioOutput::MiniaudioOutput() : impl_(std::make_unique<Impl>()) {}

MiniaudioOutput::~MiniaudioOutput() { stop(); }

bool MiniaudioOutput::initialize(int sample_rate, std::string& error) {
    if (sample_rate <= 0) {
        error = "Invalid speech sample rate.";
        return false;
    }
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 1;
    config.sampleRate = static_cast<ma_uint32>(sample_rate);
    config.dataCallback = &Impl::callback;
    config.pUserData = impl_.get();

    ma_result result = ma_device_init(nullptr, &config, &impl_->device);
    if (result != MA_SUCCESS) {
        error = std::string("Unable to open the default playback device: ") +
                ma_result_description(result);
        return false;
    }
    impl_->maximum_frames = static_cast<std::size_t>(sample_rate) * kMaximumQueuedSeconds;
    result = ma_device_start(&impl_->device);
    if (result != MA_SUCCESS) {
        error = std::string("Unable to start the default playback device: ") +
                ma_result_description(result);
        ma_device_uninit(&impl_->device);
        return false;
    }
    impl_->initialized = true;
    return true;
}

void MiniaudioOutput::stop() {
    if (!impl_ || !impl_->initialized) {
        return;
    }
    impl_->stopping.store(true, std::memory_order_release);
    ma_device_stop(&impl_->device);
    ma_device_uninit(&impl_->device);
    impl_->initialized = false;
}

bool MiniaudioOutput::enqueue(std::uint64_t generation, PcmAudio audio) {
    if (!impl_->initialized || audio.channels != 1 ||
        audio.sample_rate != static_cast<int>(impl_->device.sampleRate)) {
        return false;
    }
    const auto frames = audio.frame_count();
    while (!impl_->stopping.load(std::memory_order_acquire) &&
           generation == impl_->valid_generation.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (frames <= impl_->maximum_frames -
                              std::min(impl_->maximum_frames, impl_->queued_frames)) {
                impl_->queued_frames += frames;
                impl_->queue.push_back({generation, std::move(audio)});
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void MiniaudioOutput::set_valid_generation(std::uint64_t generation) {
    impl_->valid_generation.store(generation, std::memory_order_release);
}

void MiniaudioOutput::interrupt() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->queue.clear();
    impl_->current = {};
    impl_->has_current = false;
    impl_->current_frame = 0;
    impl_->queued_frames = 0;
}

bool MiniaudioOutput::is_drained(std::uint64_t generation) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->has_current && impl_->current.generation == generation) {
        return false;
    }
    return std::none_of(impl_->queue.begin(), impl_->queue.end(),
                        [generation](const Impl::QueuedAudio& item) {
                            return item.generation == generation;
                        });
}

}  // namespace trv
