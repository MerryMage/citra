// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>

#include "audio_core/sink.h"

namespace AudioCore {

class SDL2Sink final : public Sink {
public:
    SDL2Sink();
    ~SDL2Sink() override;

    /// The native rate of this sink. The sink expects to be fed samples that respect this. (Units: samples/sec)
    unsigned int GetNativeSampleRate() const override;

    /**
    * Feed stereo samples to sink.
    * @param samples Samples in interleaved stereo PCM16 format. Size of vector must be multiple of two.
    */
    void EnqueueSamples(const std::vector<s16>& samples) override;

    /// Samples enqueued that have not been played yet.
    size_t SamplesInQueue() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}