// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "audio_core/hle/common.h"
#include "audio_core/hle/dsp.h"
#include "audio_core/hle/mixers.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/math_util.h"

namespace DSP {
namespace HLE {

/// HACK: Prevent clipping by scaling down the volume.
constexpr float PREVENT_CLIPPING_HACK = 0.3f;

void Mixers::Reset() {
    current_frame.fill({});
    state = {};
}

DspStatus Mixers::Tick(DspConfiguration& config,
        const IntermediateMixSamples& read_samples,
        IntermediateMixSamples& write_samples,
        const std::array<QuadFrame32, 3>& input)
{
    ParseConfig(config);

    AuxReturn(read_samples);
    AuxSend(write_samples, input);

    MixCurrentFrame();

    return GetCurrentStatus();
}

void Mixers::ParseConfig(DspConfiguration& config) {
    if (!config.dirty_raw) {
        return;
    }

    if (config.mixer1_enabled_dirty) {
        config.mixer1_enabled_dirty.Assign(0);
        state.mixer1_enabled = config.mixer1_enabled != 0;
        LOG_TRACE(Audio_DSP, "mixers mixer1_enabled = %hu", config.mixer1_enabled);
    }

    if (config.mixer2_enabled_dirty) {
        config.mixer2_enabled_dirty.Assign(0);
        state.mixer2_enabled = config.mixer2_enabled != 0;
        LOG_TRACE(Audio_DSP, "mixers mixer2_enabled = %hu", config.mixer2_enabled);
    }

    if (config.volume_0_dirty) {
        config.volume_0_dirty.Assign(0);
        state.intermediate_mixer_volume[0] = config.volume[0];
        LOG_TRACE(Audio_DSP, "mixers volume[0]=%f", config.volume[0]);
    }

    if (config.volume_1_dirty) {
        config.volume_1_dirty.Assign(0);
        state.intermediate_mixer_volume[1] = config.volume[1];
        LOG_TRACE(Audio_DSP, "mixers volume[1]=%f", config.volume[1]);
    }

    if (config.volume_2_dirty) {
        config.volume_2_dirty.Assign(0);
        state.intermediate_mixer_volume[2] = config.volume[2];
        LOG_TRACE(Audio_DSP, "mixers volume[2]=%f", config.volume[2]);
    }

    if (config.output_format_dirty) {
        config.output_format_dirty.Assign(0);
        state.output_format = config.output_format;
        LOG_TRACE(Audio_DSP, "mixers output_format=%hu", config.output_format);
    }

    if (config.headphones_connected_dirty) {
        config.headphones_connected_dirty.Assign(0);
        // Do nothing
        LOG_TRACE(Audio_DSP, "mixers headphones_connected=%hu", config.headphones_connected);
    }

    if (config.dirty_raw) {
        LOG_DEBUG(Audio_DSP, "mixers remaining_dirty=%x", config.dirty_raw);
    }

    config.dirty_raw = 0;
}

void Mixers::DownmixAndMixIntoCurrentFrame(float gain, const QuadFrame32& samples) {
    gain *= PREVENT_CLIPPING_HACK;

    switch (state.output_format) {
    case OutputFormat::Mono:
        std::transform(current_frame.begin(), current_frame.end(), samples.begin(), current_frame.begin(),
            [gain](const std::array<s16, 2>& accumulator, const std::array<s32, 4>& sample) -> std::array<s16, 2> {
                s32 mono = static_cast<s32>(gain * (sample[0] + sample[1] + sample[2] + sample[3]) / 2);
                mono = MathUtil::Clamp(mono, -32768, 32767);
                return { accumulator[0] + static_cast<s16>(mono), accumulator[1] + static_cast<s16>(mono) };
            });
        return;
    case OutputFormat::Surround: // TODO(merry): Implement surround sound.
    case OutputFormat::Stereo:
        std::transform(current_frame.begin(), current_frame.end(), samples.begin(), current_frame.begin(),
            [gain](const std::array<s16, 2>& accumulator, const std::array<s32, 4>& sample) -> std::array<s16, 2> {
                s32 left = static_cast<s32>(gain * (sample[0] + sample[2]));
                s32 right = static_cast<s32>(gain * (sample[1] + sample[3]));
                left = MathUtil::Clamp(left, -32768, 32767);
                right = MathUtil::Clamp(right, -32768, 32767);
                return { accumulator[0] + static_cast<s16>(left), accumulator[1] + static_cast<s16>(right) };
            });
        return;
    }

    UNREACHABLE();
}

void Mixers::AuxReturn(const IntermediateMixSamples& read_samples) {
    // NOTE: read_samples.mix{1,2} annoyingly have their dimensions in reverse order to QuadFrame32.

    if (state.mixer1_enabled) {
        for (size_t sample = 0; sample < samples_per_frame; sample++) {
            for (size_t channel = 0; channel < 4; channel++) {
                state.intermediate_mix_buffer[1][sample][channel] = read_samples.mix1.pcm32[channel][sample];
            }
        }
    }

    if (state.mixer2_enabled) {
        for (size_t sample = 0; sample < samples_per_frame; sample++) {
            for (size_t channel = 0; channel < 4; channel++) {
                state.intermediate_mix_buffer[2][sample][channel] = read_samples.mix2.pcm32[channel][sample];
            }
        }
    }
}

void Mixers::AuxSend(IntermediateMixSamples& write_samples, const std::array<QuadFrame32, 3> input) {
    // NOTE: read_samples.mix{1,2} annoyingly have their dimensions in reverse order to QuadFrame32.

    state.intermediate_mix_buffer[0] = input[0];

    if (state.mixer1_enabled) {
        for (size_t sample = 0; sample < samples_per_frame; sample++) {
            for (size_t channel = 0; channel < 4; channel++) {
                write_samples.mix1.pcm32[channel][sample] = input[1][sample][channel];
            }
        }
    } else {
        state.intermediate_mix_buffer[1] = input[1];
    }

    if (state.mixer2_enabled) {
        for (size_t sample = 0; sample < samples_per_frame; sample++) {
            for (size_t channel = 0; channel < 4; channel++) {
                write_samples.mix2.pcm32[channel][sample] = input[2][sample][channel];
            }
        }
    } else {
        state.intermediate_mix_buffer[2] = input[2];
    }
}

void Mixers::MixCurrentFrame() {
    //TODO(merry): Compressor and limiter.

    current_frame.fill({});
    for (size_t mix = 0; mix < 3; mix++) {
        DownmixAndMixIntoCurrentFrame(state.intermediate_mixer_volume[mix], state.intermediate_mix_buffer[mix]);
    }
}

DspStatus Mixers::GetCurrentStatus() const {
    DspStatus status;
    status.unknown = 0;
    status.dropped_frames = 0; // LLE required.
    return status;
}

} // namespace HLE
} // namespace DSP
