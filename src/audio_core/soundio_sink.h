// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <vector>

#include <boost/lockfree/spsc_queue.hpp>

#include "audio_core/sink.h"
#include "common/common_types.h"

struct SoundIo;
struct SoundIoDevice;
struct SoundIoOutStream;

namespace AudioCore {

    /// The class implements a Sink using libsoundio (a cross-platform sound I/O library) for sound output.
    class SoundIoSink : public Sink {
    public:
        /// Initialises SoundIo with a default backend and default device.
        SoundIoSink();

        /// Shuts down SoundIo.
        ~SoundIoSink() override;

        /// The native rate of this sink. The sink expects to be fed samples that respect this. (Units: samples/sec)
        int GetNativeSampleRate() const override;

        /**
        * Feed stereo samples to sink.
        * @param left Left stereo channel, signed PCM16 format
        * @param right Right stereo channel, signed PCM16 format
        * Assumption: left.size() == right.size()
        * This function must only be called by a single thread.
        */
        void EnqueueSamples(const std::vector<s16>& left, const std::vector<s16>& right) override;

        /// Samples enqueued that have not been played yet.
        std::size_t SamplesInQueue() const override;

    private:
        struct Buffer {
            std::vector<s16> left;
            std::vector<s16> right;
            std::size_t size();
        };

        /// Communication queue between other threads and the audio device thread.
        boost::lockfree::spsc_queue<Buffer, boost::lockfree::capacity<16>> buffer_queue;

        /// How many samples remain in the queue
        std::atomic_size_t samples_in_queue;

        /// This callback is called by SoundIo when audio data is being requested by the audio device. (See: SoundIoSink::SoundIoSink())
        static void WriteCallback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);

        // libsoundio structures
        SoundIo *soundio;
        SoundIoDevice *device;
        SoundIoOutStream *outstream;
    };

} // namespace
