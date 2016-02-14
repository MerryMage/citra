// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include <soundio.h>

#include "audio_core/audio_core.h"
#include "audio_core/soundio_sink.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/math_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace AudioCore {

SoundIoSink::SoundIoSink() {
    int err;

    LOG_DEBUG(Audio, "Backend: %s", soundio_version_string());

    soundio = soundio_create();
    if (!soundio) {
        LOG_CRITICAL(Audio, "Failed to create soundio object");
        exit(-1);
    }

    soundio->app_name = "Citra"; // Pretty display name (must not contain a colon).
    // Note: Default on_backend_disconnect crashes the program. TODO(merry): Clean disconnect.
    soundio->userdata = (void*) this;

    // Get backend

    err = soundio_connect(soundio);
    if (err) {
        LOG_CRITICAL(Audio, "Unable to connect to backend: %s", soundio_strerror(err));
        exit(-1);
    }

    LOG_INFO(Audio, "Using audio backend: %s", soundio_backend_name(soundio->current_backend));

    // Get output device

    soundio_flush_events(soundio);

    int device_index = soundio_default_output_device_index(soundio);
    if (device_index < 0) {
        LOG_CRITICAL(Audio, "Unable to find audio output device");
        exit(-1);
    }

    device = soundio_get_output_device(soundio, device_index);
    if (!device) {
        LOG_CRITICAL(Audio, "Failed to create soundiodevice object");
        exit(-1);
    }

    LOG_INFO(Audio, "Using audio device: %s", device->name);
    LOG_INFO(Audio, "Minimum/Current/Maximum software latency (seconds): %f/%f/%f", device->software_latency_min, device->software_latency_current, device->software_latency_max);

    if (device->probe_error) {
        LOG_CRITICAL(Audio, "Failed to probe audio output device: %s", soundio_strerror(device->probe_error));
        exit(-1);
    }

    // Configure and open output stream on device

    outstream = soundio_outstream_create(device);
    outstream->name = "Citra audio output"; // Pretty display name (must not contain a colon).
    outstream->format = SoundIoFormatS16NE; // Request signed 16-bit ints in native endian format.
    outstream->software_latency = device->software_latency_min; // Request as low a software latency as possible.
    outstream->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo); // Request stereo. Pointer is to statically allocated structure, do not free().
    outstream->sample_rate = 48000; // Request a reasonable standard sample rate we are able to upsample to.
    // TODO(merry): Consider requesting sample rate close to AudioCore::native_sample_rate, and if you're able to, skip final resampling.

    outstream->write_callback = &SoundIoSink::WriteCallback;
    outstream->userdata = (void*)this;
    // Note: We do not bother with an underflow_callback, the default of playing silence is fine.
    // Note: The default error_callback calls abort if a stream error occurs. This is undesireable, we may prefer to just have no audio if that ever happens.
    // TODO(merry): Implement an error_callback.

    err = soundio_outstream_open(outstream);
    if (err) {
        LOG_CRITICAL(Audio, "Unable to open stream on audio output device: %s", soundio_strerror(err));
        exit(-1);
    }

    if (outstream->layout_error) {
        LOG_CRITICAL(Audio, "Failed to set stream layout (requested: stereo) on audio output device: %s", soundio_strerror(outstream->layout_error));
        exit(-1);
    }

    if (outstream->sample_rate < AudioCore::native_sample_rate) {
        LOG_CRITICAL(Audio, "Device requires sample rate of %d. This would require downsampling which we do not implement. Abort.", outstream->sample_rate);
        exit(-1);
    }

    err = soundio_outstream_start(outstream);
    if (err) {
        LOG_CRITICAL(Audio, "Failed to start stream on audio output device: %s", soundio_strerror(err));
    }
}

SoundIoSink::~SoundIoSink() {
    soundio_outstream_destroy(outstream);
    soundio_device_unref(device);
    soundio_destroy(soundio);
}

int SoundIoSink::GetNativeSampleRate() const {
    return outstream->sample_rate;
}

void SoundIoSink::EnqueueSamples(const std::vector<s16>& left, const std::vector<s16>& right) {
    DEBUG_ASSERT(left.size() == right.size());

    buffer_queue.push({ left, right });
    samples_in_queue += left.size();
}

std::size_t SoundIoSink::Buffer::size() {
    DEBUG_ASSERT(left.size() == right.size());

    return left.size();
}


std::size_t SoundIoSink::SamplesInQueue() const {
    return samples_in_queue;
}

void SoundIoSink::WriteCallback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max) {
    // This will be called from within an audio device thread.
    // This needs to be fast and should not block (do not write to disk, print to console, &c).
    // This function must write at least frame_count_min frames and at most frame_count_max frames.

    SoundIoSink* _this = reinterpret_cast<SoundIoSink*>(outstream->userdata);

    struct SoundIoChannelArea *areas;
    int err;

    auto write_buffer = [&](Buffer& buf) {
        // Here we request permission to write frame_count frames.
        // libsoundio then tells us how many frames we're able to write (at most the amount we request) and updates the areas variable.
        int frame_count = MathUtil::Clamp((int)buf.size(), 0, frame_count_max);
        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            LOG_CRITICAL(Audio, "%s", soundio_strerror(err)); //TODO(merry): Cleaner shutdown
            exit(-2);
        }
        frame_count_min -= frame_count;
        frame_count_max -= frame_count;

        // Write the frames to the relevant channel areas.
        for (int frame = 0; frame < frame_count; frame++) {
            *reinterpret_cast<s16*>(areas[0].ptr + areas[0].step * frame) = buf.left[frame];
            *reinterpret_cast<s16*>(areas[1].ptr + areas[1].step * frame) = buf.right[frame];
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            LOG_CRITICAL(Audio, "%s", soundio_strerror(err)); //TODO(merry): Cleaner shutdown
            exit(-2);
        }

        // Remove samples that we have written from the buffer.
        buf.left.erase(buf.left.begin(), buf.left.begin() + frame_count);
        buf.right.erase(buf.right.begin(), buf.right.begin() + frame_count);
    };

    while (_this->buffer_queue.read_available() > 0 && frame_count_max > 0) {
        auto& buffer = _this->buffer_queue.front();

        // Write as much as possible from this buffer
        while (buffer.size() != 0 && frame_count_max > 0) {
            write_buffer(buffer);
        }

        // Remove empty buffers
        if (buffer.size() == 0) {
            _this->buffer_queue.pop();
        }
    }

    // Ensure at least frame_count_min frames are written.
    // If we haven't managed to fill them up with real samples fill them up with silence.
    while (frame_count_min > 0) {
        // Request permission to write this many frames.
        int frame_count = frame_count_min;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            LOG_CRITICAL(Audio, "%s", soundio_strerror(err)); //TODO(merry): Cleaner shutdown
            exit(-2);
        }
        frame_count_min -= frame_count;
        frame_count_max -= frame_count;

        // Zero-filled frames.
        for (int frame = 0; frame < frame_count; frame++) {
            *reinterpret_cast<s16*>(areas[0].ptr + areas[0].step * frame) = 0;
            *reinterpret_cast<s16*>(areas[1].ptr + areas[1].step * frame) = 0;
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            LOG_CRITICAL(Audio, "%s", soundio_strerror(err)); //TODO(merry): Cleaner shutdown
            exit(-2);
        }
    }

    DEBUG_ASSERT(frame_count_min == 0);
    DEBUG_ASSERT(frame_count_max >= 0);
}

} // namespace
