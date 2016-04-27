// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>

#include "audio_core/audio_core.h"
#include "audio_core/sink.h"

namespace AudioCore {

class NullSink final : public Sink {
public:
    ~NullSink() override = default;

    /// The native rate of this sink. The sink expects to be fed samples that respect this. (Units: samples/sec)
    unsigned int GetNativeSampleRate() const override {
        return AudioCore::native_sample_rate;
    }

    /**
     * Feed stereo samples to sink.
     * @param samples Samples in interleaved stereo PCM16 format. Size of vector must be multiple of two.
     */
    void EnqueueSamples(const std::vector<s16>& samples) override {}

    /// Samples enqueued that have not been played yet.
    size_t SamplesInQueue() const override {
        return 0;
    }
};

}