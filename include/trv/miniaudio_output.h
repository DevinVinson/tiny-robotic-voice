#pragma once

#include "trv/audio.h"

#include <cstddef>
#include <memory>
#include <string>

namespace trv {

class MiniaudioOutput final : public AudioSink {
public:
    static constexpr std::size_t kMaximumQueuedSeconds = 20;

    MiniaudioOutput();
    ~MiniaudioOutput() override;
    MiniaudioOutput(const MiniaudioOutput&) = delete;
    MiniaudioOutput& operator=(const MiniaudioOutput&) = delete;

    bool initialize(int sample_rate, std::string& error);
    void stop();
    bool enqueue(std::uint64_t generation, PcmAudio audio) override;
    void set_valid_generation(std::uint64_t generation) override;
    void interrupt() override;
    [[nodiscard]] bool is_drained(std::uint64_t generation) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace trv
