// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>

#include "audio_core/hle/dsp.h"
#include "audio_core/hle/pipe.h"
#include "audio_core/hle/source.h"

namespace DSP {
namespace HLE {

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

std::array<Source, num_sources> sources = {
    Source(0), Source(1), Source(2), Source(3), Source(4), Source(5),
    Source(6), Source(7), Source(8), Source(9), Source(10), Source(11),
    Source(12), Source(13), Source(14), Source(15), Source(16), Source(17),
    Source(18), Source(19), Source(20), Source(21), Source(22), Source(23)
};

void Init() {
    DSP::HLE::ResetPipes();
    for (auto& source : sources) {
        source.Reset();
    }
}

void Shutdown() {
}

bool Tick() {
    SharedMemory& read = ReadRegion();
    SharedMemory& write = WriteRegion();

    for (size_t i = 0; i < num_sources; i++) {
        write.source_statuses.status[i] = sources[i].Tick(read.source_configurations.config[i], read.adpcm_coefficients.coeff[i]);
        for (size_t mix = 0; mix < 3; mix++) {
            //source[i].MixInto(/*...*/, mix);
        }
    }

    return true;
}

} // namespace HLE
} // namespace DSP
