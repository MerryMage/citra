// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <memory>
#include <queue>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include "common/logging/log.h"
#include "common/math_util.h"

#include "core/audio/audio.h"


namespace Audio {

using ALCDevicePointer = std::unique_ptr<ALCdevice, decltype(&alcCloseDevice)>;
using ALCContextPointer = std::unique_ptr<ALCcontext, decltype(&alcDestroyContext)>;

static ALCDevicePointer device = ALCDevicePointer(nullptr, nullptr);
static ALCContextPointer context = ALCContextPointer(nullptr, nullptr);

static const int BASE_SAMPLE_RATE = 22050;

struct Buffer {
    u16 id; ///< buffer_id that userland gives us
    ALuint buffer;
    bool is_looping;
    bool operator < (const Buffer& other) const {
        // We want things with lower id to appear first, unless we have wraparound.
        // priority_queue puts a before b when b < a.
        // Should perhaps be a functor instead.
        if ((other.id - id) > 1000) return true;
        if ((id - other.id) > 1000) return false;
        return id > other.id;
    }
};

struct AdpcmState {
    // Two historical samples from previous processed buffer
    s16 yn1; ///< y[n-1]
    s16 yn2; ///< y[n-2]
};

// GC-ADPCM with scale factor and variable coefficients.
// Frames are 8 bytes long containing 14 samples each.
// Samples are 4 bits (one nybble) long.
static std::vector<s16> DecodeADPCM(const u8 * const data, const size_t sample_count, const std::array<s16, 16>& adpcm_coeff, AdpcmState& state) {
    constexpr size_t FRAME_LEN = 8;
    constexpr size_t SAMPLES_PER_FRAME = 14;
    constexpr int SIGNED_NYBBLES[16] = {0,1,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1};

    std::vector<s16> ret(sample_count);
    int yn1 = state.yn1, yn2 = state.yn2;

    const int NUM_FRAMES = (sample_count + (SAMPLES_PER_FRAME-1)) / SAMPLES_PER_FRAME; // Round up.
    for (int framei = 0; framei < NUM_FRAMES; framei++) {
        int frame_header = data[framei * FRAME_LEN];
        int scale = 1 << (frame_header & 0xF);
        int idx = (frame_header >> 4) & 0x7;

        // Coefficients are fixed point with 11 bits fractional part.
        int coef1 = adpcm_coeff[idx * 2 + 0];
        int coef2 = adpcm_coeff[idx * 2 + 1];

        // Decodes an audio sample. One nybble produces one s16 sample.
        auto decode_sample = [&](int nybble) -> s16 {
            int xn = nybble * scale;
            // We first transform everything into 11 bit fixed point, perform the second order digital filter, then transform back.
            // 0x400 == 0.5 in 11 bit fixed point.
            // Filter: y[n] = x[n] + 0.5 + c1 * y[n-1] + c2 * y[n-2]
            int val = ((xn << 11) + 0x400 + coef1 * yn1 + coef2 * yn2) >> 11;
            // Clamp to output range.
            val = MathUtil::Clamp(val, -32768, 32767);
            // Advance output feedback.
            yn2 = yn1;
            yn1 = val;
            return (s16)val;
        };

        int outputi = framei * SAMPLES_PER_FRAME;
        int datai = framei * FRAME_LEN + 1;
        for (int i = 0; i < SAMPLES_PER_FRAME && outputi < sample_count; i += 2) {
            ret[outputi++] = decode_sample(SIGNED_NYBBLES[data[datai] & 0xF]);
            ret[outputi++] = decode_sample(SIGNED_NYBBLES[data[datai] >> 4]);
            datai++;
        }
    }

    state.yn1 = yn1;
    state.yn2 = yn2;

    return ret;
}

struct OutputChannel {
    ~OutputChannel() {
        alDeleteSources(1, &source);
        while (!queue.empty()) {
            alDeleteBuffers(1, &queue.top().buffer);
            queue.pop();
        }
        while (!playing.empty()) {
            alDeleteBuffers(1, &playing.front().buffer);
            playing.pop();
        }
    }

    ALuint source;                     ///< Each channel has it's own output, we lean on OpenAL to do our mixing.

    // Configuration
    int mono_or_stereo;                ///< Value from userland. 1 == mono, 2 == stereo, other == ???
    Format format;
    bool enabled;                      ///< Userland wants us to remind them we have enabled this channel.
    bool was_fed_data;                 ///< Userland wants to know if we have been fed data.

    // Buffer management
    std::priority_queue<Buffer> queue; ///< Things we have gotten from userland we haven't queued onto `source` yet.
    std::queue<Buffer> playing;        ///< Things we have queued onto `source`.
    u16 last_buffer_id;                ///< Userland wants us to report back what was the thing we last played.

    // For ADPCM decoding use.
    std::array<s16, 16> adpcm_coeffs;
    AdpcmState adpcm_state;
};

static std::array<OutputChannel, 24> chans;

int InitAL() {
    device = ALCDevicePointer(alcOpenDevice(nullptr), &alcCloseDevice);
    if (!device) {
        LOG_CRITICAL(Audio, "Could not open a device!");
        return 1;
    }

    context = ALCContextPointer(alcCreateContext(device.get(), nullptr), &alcDestroyContext);
    if (context == nullptr || alcMakeContextCurrent(context.get()) == ALC_FALSE) {
        context.reset();
        device.reset();
        LOG_CRITICAL(Audio, "Could not set a context!");
        return 1;
    }

    LOG_INFO(Audio, "Audio output is on \"%s\"", alcGetString(device.get(), ALC_DEVICE_SPECIFIER));
    return 0;
}

static ALCint dev_rate;                    ///< Native sample rate of our output device
static std::array<u8, 10000> silence = {}; ///< Some silence, used if an audio error occurs

void Init() {
    InitAL();

    alcGetIntegerv(device.get(), ALC_FREQUENCY, 1, &dev_rate);
    if (alcGetError(device.get()) != ALC_NO_ERROR) {
        LOG_CRITICAL(Audio, "Failed to get device sample rate");
    }
    LOG_INFO(Audio, "Device Frequency: %i", dev_rate);

    for (int i = 0; i < 24; i++) {
        alGenSources(1, &chans[i].source);
        if (alGetError() != AL_NO_ERROR) {
            LOG_CRITICAL(Audio, "Channel %i: Failed to setup sound source", i);
        }
    }
}

void Shutdown() {
    alcMakeContextCurrent(nullptr);
    context.reset();
    device.reset();
}

void UpdateFormat(int channel_id, int mono_or_stereo, Format format) {
    chans[channel_id].mono_or_stereo = mono_or_stereo;
    chans[channel_id].format = format;
}

void UpdateAdpcm(int channel_id, s16 coeffs[16]) {
    LOG_DEBUG(Audio, "Channel %i: ADPCM Coeffs updated", channel_id);
    std::copy(coeffs, coeffs+16, std::begin(chans[channel_id].adpcm_coeffs));
}

void SilenceBuffer(ALuint& b, int channel_id, u16 buffer_id) {
    if (alGetError() != AL_NO_ERROR) {
        LOG_CRITICAL(Audio, "Channel %i: Buffer %u: OpenAL says \"%s\"", channel_id, buffer_id, alGetString(alGetError()));
    }
    alBufferData(b, AL_FORMAT_MONO8, silence.data(), silence.size(), BASE_SAMPLE_RATE);
    if (alGetError() != AL_NO_ERROR) {
        LOG_CRITICAL(Audio, "Channel %i: Failed to init silence buffer!!! (%s)", channel_id, alGetString(alGetError()));
    }
}

void EnqueueBuffer(int channel_id, u16 buffer_id, const u8* data, int sample_count, bool is_looping) {
    LOG_DEBUG(Audio, "Channel %i: Buffer %u: Enqueued (size %i)", channel_id, buffer_id, sample_count);

    if (is_looping) {
        LOG_WARNING(Audio, "Channel %i: Buffer %u: Looped buffers are unimplemented", channel_id, buffer_id);
    }

    auto& c = chans[channel_id];
    c.was_fed_data = true;

    ALuint b;
    alGenBuffers(1, &b);

    switch(c.format) {
    case Format::PCM16:
        switch (c.mono_or_stereo) {
        case 2:
            alBufferData(b, AL_FORMAT_STEREO16, data, sample_count * 4, BASE_SAMPLE_RATE);
            break;
        case 1:
        default:
            alBufferData(b, AL_FORMAT_MONO16, data, sample_count * 2, BASE_SAMPLE_RATE);
            break;
        }

        if (alGetError() != AL_NO_ERROR) goto do_silence;

        break;
    case Format::PCM8:
        switch (c.mono_or_stereo) {
        case 2:
            alBufferData(b, AL_FORMAT_STEREO8, data, sample_count * 2, BASE_SAMPLE_RATE);
            break;
        case 1:
        default:
            alBufferData(b, AL_FORMAT_MONO8, data, sample_count * 1, BASE_SAMPLE_RATE);
            break;
        }

        if (alGetError() != AL_NO_ERROR) goto do_silence;

        break;
    case Format::ADPCM: {
        if (c.mono_or_stereo != 1) {
            LOG_ERROR(Audio, "Channel %i: Buffer %u: Being fed non-mono ADPCM (size: %i samples)", channel_id, buffer_id, sample_count);
        }

        std::vector<s16> decoded = DecodeADPCM(data, sample_count, c.adpcm_coeffs, c.adpcm_state);
        alBufferData(b, AL_FORMAT_STEREO16, decoded.data(), decoded.size() * 2, BASE_SAMPLE_RATE);

        if (alGetError() != AL_NO_ERROR) goto do_silence;

        break;
    }
    default:
        LOG_ERROR(Audio, "Channel %i: Buffer %u: Unrecognised audio format (size: %i samples)", channel_id, buffer_id, sample_count);
        SilenceBuffer(b, channel_id, buffer_id);
        break;
    }

    c.queue.emplace(Buffer{ buffer_id, b, is_looping });

    if (c.queue.size() > 10) {
        LOG_ERROR(Audio, "We have far far too many buffers enqueued on channel %i (%zu of them)", channel_id, c.queue.size());
    }
}

void Play(int channel_id, bool play) {
    if (play) {
        LOG_INFO(Audio, "Channel %i: Enabled", channel_id);
    } else {
        LOG_INFO(Audio, "Channel %i: Disabled", channel_id);
        chans[channel_id].was_fed_data = false;
    }
    chans[channel_id].enabled = play;
}

void Tick(int channel_id) {
    auto& c = chans[channel_id];

    if (!c.queue.empty()) {
        while (!c.queue.empty()) {
            alSourceQueueBuffers(c.source, 1, &c.queue.top().buffer);
            if (alGetError() != AL_NO_ERROR) {
                alDeleteBuffers(1, &c.queue.top().buffer);
                LOG_CRITICAL(Audio, "Channel %i: Buffer %u: Failed to enqueue : %s", channel_id, c.queue.top().id, alGetString(alGetError()));
                c.queue.pop();
                continue;
            }
            c.playing.emplace(c.queue.top());
            c.queue.pop();
        }
        if (c.enabled) {
            ALint state;
            alGetSourcei(c.source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                alSourcePlay(c.source);
            }
        }
    }

    if (chans[channel_id].playing.size() > 10) {
        LOG_ERROR(Audio, "Channel %i: We have far far too many buffers enqueued (%zu of them)", channel_id, chans[channel_id].playing.size());
    }

    ALint processed;
    alGetSourcei(c.source, AL_BUFFERS_PROCESSED, &processed);
    while (processed > 0) {
        ALuint buf;
        alSourceUnqueueBuffers(c.source, 1, &buf);
        processed--;

        if (!c.playing.empty()) {
            if (c.playing.front().buffer != buf) {
                LOG_CRITICAL(Audio, "Channel %i: Play queue desynced with OpenAL queue. (buf???)", channel_id);
            } else {
                LOG_DEBUG(Audio, "Channel %i: Buffer %u: Finished playing", channel_id, c.playing.front().id);
            }
            c.last_buffer_id = c.playing.front().id;
            c.playing.pop();
        } else {
            LOG_CRITICAL(Audio, "Channel %i: Play queue desynced with OpenAL queue. (empty)", channel_id);
        }

        alDeleteBuffers(1, &buf);
    }

    if (!c.playing.empty()) {
        c.last_buffer_id = c.playing.front().id;
    }
}

ChannelStatus GetStatus(int channel_id) {
    auto& c = chans[channel_id];

    ChannelStatus ret;
    ret.is_enabled = c.enabled;
    ret.most_recent_buffer_id = c.last_buffer_id;
    ret.was_fed_data = c.was_fed_data;

    ALint state, samples;
    alGetSourcei(c.source, AL_SOURCE_STATE, &state);
    alGetSourcei(c.source, AL_SAMPLE_OFFSET, &samples);
    ret.sample_position = samples;

    return ret;
}

};
