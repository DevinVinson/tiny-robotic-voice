#pragma once

#include <cstdint>
#include <vector>

namespace trv {

struct PcmAudio {
    int sample_rate = 0;
    int channels = 1;
    std::vector<int16_t> samples;

    [[nodiscard]] std::size_t frame_count() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
};

class AudioSink {
public:
    virtual ~AudioSink() = default;
    virtual bool enqueue(std::uint64_t generation, PcmAudio audio) = 0;
    virtual void set_valid_generation(std::uint64_t generation) = 0;
    virtual void interrupt() = 0;
    [[nodiscard]] virtual bool is_drained(std::uint64_t generation) const = 0;
};

}  // namespace trv
