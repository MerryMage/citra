// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>

#include "audio_core/hle/dsp.h"
#include "audio_core/hle/mixers.h"
#include "audio_core/hle/pipe.h"
#include "audio_core/hle/source.h"
#include "audio_core/time_stretch.h"

#include "core/hle/service/dsp_dsp.h"
#include <audio_core/null_sink.h>

namespace DSP {
namespace HLE {

// Region management

std::array<SharedMemory, 2> g_regions;

static size_t CurrentRegionIndex() {
    // The region with the higher frame counter is chosen unless there is wraparound.
    // This function only returns a 0 or 1.

    if (g_regions[0].frame_counter == 0xFFFFu && g_regions[1].frame_counter != 0xFFFEu) {
        // Wraparound has occured.
        return 1;
    }

    if (g_regions[1].frame_counter == 0xFFFFu && g_regions[0].frame_counter != 0xFFFEu) {
        // Wraparound has occured.
        return 0;
    }

    return (g_regions[0].frame_counter > g_regions[1].frame_counter) ? 0 : 1;
}

static SharedMemory& ReadRegion() {
    return g_regions[CurrentRegionIndex()];
}

static SharedMemory& WriteRegion() {
    return g_regions[1 - CurrentRegionIndex()];
}

// Audio processing and mixing

static std::array<Source, num_sources> sources = {
    Source(0), Source(1), Source(2), Source(3), Source(4), Source(5),
    Source(6), Source(7), Source(8), Source(9), Source(10), Source(11),
    Source(12), Source(13), Source(14), Source(15), Source(16), Source(17),
    Source(18), Source(19), Source(20), Source(21), Source(22), Source(23)
};
static Mixers mixers;

static StereoFrame16 GenerateCurrentFrame() {
    SharedMemory& read = ReadRegion();
    SharedMemory& write = WriteRegion();

    std::array<QuadFrame32, 3> intermediate_mixes = {{{}}};

    for (size_t i = 0; i < num_sources; i++) {
        write.source_statuses.status[i] = sources[i].Tick(read.source_configurations.config[i], read.adpcm_coefficients.coeff[i]);
        for (size_t mix = 0; mix < 3; mix++) {
            sources[i].MixInto(intermediate_mixes[mix], mix);
        }
    }

    // TODO(merry): Reverb, Delay effects

    write.dsp_status = mixers.Tick(read.dsp_configuration, read.intermediate_mix_samples, write.intermediate_mix_samples, intermediate_mixes);

    StereoFrame16 output_frame = mixers.GetOutput();

    // Write current output frame to the shared memory region
    std::transform(output_frame.begin(), output_frame.end(), write.final_samples.pcm16.begin(),
        [](const auto& sample) -> std::array<s16_le, 2> {
            return { s16_le(sample[0]), s16_le(sample[1]) };
        });

    return output_frame;
}

// Audio output

static AudioCore::TimeStretcher time_stretcher;
static std::unique_ptr<AudioCore::Sink> sink;

void OutputCurrentFrame(const StereoFrame16& current_frame) {
    time_stretcher.AddSamples(&current_frame[0][0], current_frame.size());
    sink->EnqueueSamples(time_stretcher.Process(sink->SamplesInQueue()));
}

// Public Interface

void Init() {
    DSP::HLE::ResetPipes();
    for (auto& source : sources) {
        source.Reset();
    }
    mixers.Reset();
}

void Shutdown() {
    time_stretcher.NullSamples();
    while (true) {
        std::vector<s16> residual_audio = time_stretcher.Process(sink->SamplesInQueue());
        if (residual_audio.empty())
            break;
        sink->EnqueueSamples(residual_audio);
    }
}

bool Tick() {
    StereoFrame16 current_frame = {{}};

    if (DSP_DSP::IsSemaphoreSignalled() && GetDspState() == DspState::On) {
        // The ARM11 has finished writing to the shared memory region.
        DSP_DSP::ResetSemaphore();

        current_frame = GenerateCurrentFrame();
    }

    OutputCurrentFrame(current_frame);

    return true;
}

void SetSink(std::unique_ptr<AudioCore::Sink> sink_) {
    sink = std::move(sink_);
    time_stretcher.SetOutputSampleRate(sink->GetNativeSampleRate());
}

} // namespace HLE
} // namespace DSP
