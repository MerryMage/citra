// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "audio_core/resampler.h"
#include "core/core.h"
#include "common/math_util.h"

namespace AudioCore {

Resampler::Resampler() = default;
Resampler::~Resampler() = default;

void Resampler::SetOutputSampleRate(unsigned int sample_rate) {
    output_rate = sample_rate;
}

void Resampler::AddSamples(const s16* samples, size_t num_frames) {
    const size_t limit_index = read_index.load();
    const size_t index = write_index.load();

    // If we're trying to feed too many samples, clip off the excess.
    const size_t remaining_space_in_buffer = ((limit_index - index) & index_mask) - 1;
    num_frames = std::min(num_frames, remaining_space_in_buffer);

    const size_t overflow = buffer_size - index & index_mask;
    if (overflow < num_frames) {
        memcpy(&buffer[(index & index_mask) * 2], samples, overflow * sizeof(s16) * 2);
        memcpy(&buffer[0], samples + 2 * overflow, (num_frames - overflow) * 2 * sizeof(s16));
    } else {
        memcpy(&buffer[(index & index_mask) * 2], samples, num_frames * sizeof(s16) * 2);
    }

    write_index.fetch_add(num_frames);
    samples_added.fetch_add(num_frames);
}

std::function<void(s16*, size_t)> Resampler::GetOutputCallback() {
    return [this](s16* samples, size_t num_frames) {
        size_t moo = samples_added.load();
        double current_ratio = static_cast<double>(moo) / static_cast<double>(num_frames);
        samples_added.fetch_sub(moo);

        speed += 0.0003 * (current_ratio - speed);

        const size_t limit_index = write_index.load();
        size_t index = read_index.load();

        const size_t buffer_contents = limit_index - index;
        const double buffer_fraction = buffer_contents / static_cast<double>(buffer_size);
        const double adj = (buffer_fraction >= 0.5) ? 1.0 : (1.0 + 1.3 * (buffer_fraction - 0.5));
        const double factor = std::max(output_rate * adj * speed / static_cast<double>(native_sample_rate), 0.01);

        constexpr u64 scale_factor = 1 << 24;
        const u64 step_size = static_cast<u64>(factor * scale_factor);

        size_t in = 0, out = 0;

        // Interpolate
        const auto resample_sample = [&](size_t i) {
            int s1 = buffer[((index+in-1)&index_mask)*2 + i];
            int s2 = buffer[((index+in)&index_mask)*2 + i];

            s64 delta = MathUtil::Clamp<s64>(s2 - s1, -32768, 32767);

            int sample = s1 + delta * resample_frac / scale_factor;
            samples[out*2 + i] = MathUtil::Clamp(sample, -32768, 32767);
        };
        while (out < num_frames && in < buffer_contents - 1) {
            resample_sample(0);
            resample_sample(1);
            resample_frac += step_size;
            in += resample_frac / scale_factor;
            resample_frac %= scale_factor;
            out++;
        }

        printf("buffer_contents %zu\n", buffer_contents);
        printf("buffer_fraction %f\n", buffer_fraction);
        printf("factor %f\n", factor);
        printf("in %zu\n", in);
        printf("out %zu\n", out);

        // Pad
        while (out < num_frames) {
            samples[out*2 + 0] = buffer[in*2 + 0];
            samples[out*2 + 1] = buffer[in*2 + 1];
            out++;
        }

        read_index.fetch_add(std::min(in, buffer_contents));
    };
}

} // namespace AudioCore
