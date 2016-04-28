// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstddef>
#include <memory>
#include <vector>

#include "common/common_types.h"

namespace AudioCore {

class TimeStretcher final {
public:
    TimeStretcher();
    ~TimeStretcher();

    /**
     * Set sample rate for the samples that Process returns.
     * @param sample_rate The sample rate.
     */
    void SetOutputSampleRate(unsigned int sample_rate);

    /**
     * Adds samples to be processed.
     * @param sample_buffer Buffer of samples in interleaved stereo PCM16 format.
     * @param num_sample Number of samples.
     */
    void AddSamples(const s16* sample_buffer, size_t num_samples);

    /**
     * Flush out audio remaining in internal buffers.
     */
    void Flush();

    /**
     * Does the audio-stretching.
     * Timer calculations use sample_delay to determine how much of a margin we have.
     * @param sample_delay How many samples are buffered downstream of this module and haven't been played yet.
     * @return Samples to play in interleaved stereo PCM16 format.
     */
    std::vector<s16> Process(size_t sample_delay);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    static double ClampRatio(double ratio);
    double CalculateCurrentRatio();
    double CorrectForUnderAndOverflow(double ratio, size_t sample_delay) const;
    std::vector<s16> GetSamples();
};

} // namespace AudioCore
