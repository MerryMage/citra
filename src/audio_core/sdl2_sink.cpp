// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <vector>

#include <SDL.h>

#include "audio_core/audio_core.h"
#include "audio_core/sdl2_sink.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include <numeric>

namespace AudioCore {

struct SDL2Sink::Impl {
    unsigned sample_rate = 0;

    SDL_AudioDeviceID audio_device_id = 0;

    std::list<std::vector<s16>> queue;

    static void Callback(void* impl_, u8* buffer, int buffer_size_in_bytes);
};

SDL2Sink::SDL2Sink() : impl(new Impl) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        LOG_CRITICAL(Audio_Sink, "SDL_Init(SDL_INIT_AUDIO) failed");
        impl->audio_device_id = 0;
        return;
    }

    SDL_AudioSpec desired_audiospec;
    SDL_zero(desired_audiospec);
    desired_audiospec.format = AUDIO_S16;
    desired_audiospec.channels = 2;
    desired_audiospec.freq = AudioCore::native_sample_rate;
    desired_audiospec.samples = 1024;
    desired_audiospec.userdata = impl.get();
    desired_audiospec.callback = &SDL2Sink::Impl::Callback;

    SDL_AudioSpec obtained_audiospec;
    SDL_zero(obtained_audiospec);

    impl->audio_device_id = SDL_OpenAudioDevice(nullptr, /*iscapture=*/false, &desired_audiospec, &obtained_audiospec, 0);
    if (impl->audio_device_id <= 0) {
        LOG_CRITICAL(Audio_Sink, "SDL_OpenAudioDevice failed");
        return;
    }

    impl->sample_rate = obtained_audiospec.freq;

    SDL_PauseAudioDevice(impl->audio_device_id, /*pause_on=*/0);
}

SDL2Sink::~SDL2Sink() {
    if (impl->audio_device_id <= 0)
        return;

    SDL_CloseAudioDevice(impl->audio_device_id);
}

/// The native rate of this sink. The sink expects to be fed samples that respect this. (Units: samples/sec)
unsigned SDL2Sink::GetNativeSampleRate() const {
    if (impl->audio_device_id <= 0)
        return AudioCore::native_sample_rate;

    return impl->sample_rate;
}

/**
* Feed stereo samples to sink.
* @param samples Samples in interleaved stereo PCM16 format. Size of vector must be multiple of two.
*/
void SDL2Sink::EnqueueSamples(const std::vector<s16>& samples) {
    if (impl->audio_device_id <= 0)
        return;

    ASSERT(samples.size() % 2 == 0);

    SDL_LockAudioDevice(impl->audio_device_id);
    impl->queue.emplace_back(samples);
    SDL_UnlockAudioDevice(impl->audio_device_id);
}

/// Samples enqueued that have not been played yet.
size_t SDL2Sink::SamplesInQueue() const {
    if (impl->audio_device_id <= 0)
        return 0;

    SDL_LockAudioDevice(impl->audio_device_id);

    size_t total_size = std::accumulate(impl->queue.begin(), impl->queue.end(), 0ull, [](size_t sum, const auto& buffer) {
        // Division by two because each stereo sample is made of two s16.
        return sum + buffer.size() / 2;
    });

    SDL_UnlockAudioDevice(impl->audio_device_id);

    return total_size;
}

void SDL2Sink::Impl::Callback(void* impl_, u8* buffer, int buffer_size_in_bytes) {
    SDL2Sink::Impl* impl = reinterpret_cast<SDL2Sink::Impl*>(impl_);

    size_t remaining_size = buffer_size_in_bytes / sizeof(s16); // Convert from u8 to s16.

    while (remaining_size > 0 && !impl->queue.empty()) {
        if (impl->queue.front().size() <= remaining_size) {
            memcpy(buffer, impl->queue.front().data(), impl->queue.front().size() * sizeof(s16));
            buffer += impl->queue.front().size() * sizeof(s16);
            remaining_size -= impl->queue.front().size();
            impl->queue.pop_front();
        } else {
            memcpy(buffer, impl->queue.front().data(), remaining_size * sizeof(s16));
            buffer += remaining_size * sizeof(s16);
            impl->queue.front().erase(impl->queue.front().begin(), impl->queue.front().begin() + remaining_size);
            remaining_size = 0;
        }
    }

    if (remaining_size > 0) {
        memset(buffer, 0, remaining_size * sizeof(s16));
    }
}

}
