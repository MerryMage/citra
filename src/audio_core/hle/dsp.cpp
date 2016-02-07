// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <numeric>
#include <type_traits>

#include "audio_core/hle/dsp.h"

#include "core/core_timing.h"
#include "core/mmio.h"

namespace DSP {
namespace HLE {

SharedMemory region0;
SharedMemory region1;

void Init() {
    // STUB
}

void Shutdown() {
    // STUB
}

void Tick() {
    // STUB
}

// The region with the higher frame counter is chosen unless there is wraparound.
SharedMemory& CurrentRegion() {
    if (region0.frame_counter == 0xFFFFu && region1.frame_counter != 0xFFFEu) {
        // Wraparound has occured.
        return region1;
    }

    if (region1.frame_counter == 0xFFFFu && region0.frame_counter != 0xFFFEu) {
        // Wraparound has occured.
        return region0;
    }

    if (region0.frame_counter > region1.frame_counter) {
        return region0;
    } else {
        return region1;
    }
}

} // namespace HLE
} // namespace DSP
