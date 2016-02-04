// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <type_traits>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

namespace DSP {
namespace HLE {

/**
 * The userland-accessible region of DSP memory constists of two parts.
 * Both are marked as IO and have Read/Write permissions.
 *
 * First Region:  0x1FF50000 (Size: 0x8000)
 * Second Region: 0x1FF70000 (Size: 0x8000)
 *
 * The DSP reads from each region alternately based on the frame counter for each region much like a
 * double-buffer. The frame counter is located as the very last u16 of each region and is incremented
 * each audio tick.
 */

struct SharedMemory;

constexpr VAddr region0_base = 0x1FF50000;
extern SharedMemory region0;

constexpr VAddr region1_base = 0x1FF70000;
extern SharedMemory region1;

/**
 * A small comment on the architecture of the DSP: The DSP is native 16-bit. The DSP also appears to
 * be big-endian. When reading 32-bit numbers from its memory regions, the higher and lower 16-bit
 * halves are swapped compared to the little-endian layout of the ARM11. Hence from the ARM11's point
 * of view the memory space appears to be middle-endian.
 *
 * Unusually this does not appear to be an issue for floating point numbers. The DSP makes the more
 * sensible choice of keeping that little-endian.
 *
 * dsp_u32 below implements the conversion to and from this middle-endianness.
 */
struct dsp_u32 {
    operator u32() const {
        return Convert(storage);
    }
    void operator=(u32 new_value) {
        storage = Convert(new_value);
    }
private:
    static constexpr u32 Convert(u32 value) {
        return (value << 16) | (value >> 16);
    }
    u32_le storage = 0;
};

/**
 * DSP Memory Structures:
 *
 * There are 15 structures in each memory region. A table of them in the order they appear in memory
 * is presented below
 *
 *       Pipe 2 #    First Region DSP Address   Purpose                               Control
 *       5           0x8400                     DSP Status                            DSP
 *       9           0x8410                     DSP Debug Info                        DSP
 *       6           0x8540                     Loopback Samples #1                   DSP
 *       2           0x8680                     Channel Status [24]                   DSP
 *       8           0x8710                     Compressor Related
 *       4           0x9430                     DSP Configuration                     Userland
 *       7           0x9492                     Loopback Samples #2                   DSP
 *       1           0x9E92                     Channel Configuration [24]            Userland
 *       3           0xA792                     Channel ADPCM Coefficients [24]       Userland
 *       10          0xA912                     Surround Sound Related
 *       11          0xAA12                     Surround Sound Related
 *       12          0xAAD2                     Surround Sound Related
 *       13          0xAC52                     Surround Sound Related
 *       14          0xAC5C                     Surround Sound Related
 *       0           0xBFFF                     Frame Counter                         Userland
 *
 * Note that the above addresses do vary slightly between audio firmwares observed; the addresses are
 * not fixed in stone. The addresses above are only an examplar; they're what this implementation
 * does and provides to userland.
 *
 * Application requests the DSP service to convert DSP addresses into ARM11 virtual addresses using the
 * ConvertProcessAddressFromDspDram service call. Userland seems to derive the addresses for the second
 * region via:
 *     second_region_dsp_addr = first_region_dsp_addr | 0x10000
 *
 * Applications maintain most of its own audio state, the memory region is used mainly for
 * communication and not storage of state.
 */

#define INSERT_PADDING_DSPWORDS(num_words) u16 CONCAT2(pad, __LINE__)[(num_words)]

#define ASSERT_POD_STRUCT(name, size) \
    static_assert(std::is_standard_layout<name>::value, "Structure doesn't use standard layout"); \
    static_assert(sizeof(name) == (size), "Unexpected struct size")

struct SharedMemory {
    INSERT_PADDING_DSPWORDS(0x4000); // TODO: Fill this in
};
ASSERT_POD_STRUCT(SharedMemory, 0x8000);

#undef INSERT_PADDING_DSPWORDS
#undef ASSERT_POD_STRUCT

/// Initialize DSP hardware
void Init();

/// Shutdown DSP hardware
void Shutdown();

/// Perform processing and update state on current shared memory buffer.
/// This function is called before triggering the audio interrupt.
void Tick();

} // namespace HLE
} // namespace DSP
