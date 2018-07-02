// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <vector>
#include "common/common_types.h"

namespace AudioCore {

/**
 * This class is an interface for an audio sink. An audio sink accepts samples in stereo signed
 * PCM16 format to be output. Sinks *do not* handle resampling and expect the correct sample rate.
 * They are dumb outputs.
 */
class Sink {
public:
    virtual ~Sink() = default;

    /// The native rate of this sink. The sink expects to be fed samples that respect this. (Units:
    /// samples/sec)
    virtual unsigned int GetNativeSampleRate() const = 0;

    /**
     * Set the audio data callback.
     * @param cb Function that fills the buffer with num_frames samples.
     */
    virtual void SetCallback(std::function<void(s16* buffer, size_t num_frames)> cb) = 0;
};

} // namespace AudioCore
