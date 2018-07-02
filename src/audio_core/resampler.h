// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>

#include "audio_core/audio_types.h"
#include "common/common_types.h"

namespace AudioCore {

class Resampler final {
public:
    Resampler();
    ~Resampler();

    /**
     * Set sample rate for the samples that Process returns.
     * @param sample_rate The sample rate.
     */
    void SetOutputSampleRate(unsigned int sample_rate);

    /**
     * Add samples to be processed.
     * @param sample_buffer Buffer of samples in interleaved stereo PCM16 format.
     * @param num_samples Number of samples.
     */
    void AddSamples(const s16* sample_buffer, size_t num_samples);

    /// @returns Callback for outputting audio
    std::function<void(s16*, size_t)> GetOutputCallback();

private:
    static constexpr size_t buffer_size = 0x4000;
    static constexpr size_t index_mask = 0x3FFF;

    // FIFO
    std::array<s16, buffer_size * 2> buffer{};
    std::atomic<size_t> read_index{0};
    std::atomic<size_t> write_index{1};

    unsigned int output_rate = AudioCore::native_sample_rate;
    s64 resample_frac{};

    double speed = 1.0;
    std::atomic<size_t> samples_added{0};
};

} // namespace AudioCore
