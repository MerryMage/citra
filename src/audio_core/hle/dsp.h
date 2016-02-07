// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <type_traits>

#include "audio_core/audio_core.h"

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

namespace DSP {
namespace HLE {

/**
 * The application-accessible region of DSP memory constists of two parts.
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
 * u32_dsp below implements the conversion to and from this middle-endianness.
 */

struct u32_dsp {
    u32_dsp() = default;
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
    u32_le storage;
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
 *       6           0x8540                     Final Mix Samples                     DSP
 *       2           0x8680                     Source Status [24]                    DSP
 *       8           0x8710                     Compressor Related
 *       4           0x9430                     DSP Configuration                     Application
 *       7           0x9492                     Intermediate Mix Samples              DSP + App
 *       1           0x9E92                     Source Configuration [24]             Application
 *       3           0xA792                     Source ADPCM Coefficients [24]        Application
 *       10          0xA912                     Surround Sound Related
 *       11          0xAA12                     Surround Sound Related
 *       12          0xAAD2                     Surround Sound Related
 *       13          0xAC52                     Surround Sound Related
 *       14          0xAC5C                     Surround Sound Related
 *       0           0xBFFF                     Frame Counter                         Application
 *
 * Note that the above addresses do vary slightly between audio firmwares observed; the addresses are
 * not fixed in stone. The addresses above are only an examplar; they're what this implementation
 * does and provides to applications.
 *
 * Application requests the DSP service to convert DSP addresses into ARM11 virtual addresses using the
 * ConvertProcessAddressFromDspDram service call. Applications seem to derive the addresses for the
 * second region via:
 *     second_region_dsp_addr = first_region_dsp_addr | 0x10000
 *
 * Applications maintain most of its own audio state, the memory region is used mainly for
 * communication and not storage of state.
 *
 * In the documentation below, filter and effect transfer functions are specified in the z domain.
 */

#define INSERT_PADDING_DSPWORDS(num_words) u16 CONCAT2(pad, __LINE__)[(num_words)]

#define ASSERT_POD_STRUCT(name, size) \
    static_assert(std::is_standard_layout<name>::value, "Structure doesn't use standard layout"); \
    static_assert(sizeof(name) == (size), "Unexpected struct size")

struct Buffer {
    u32_dsp physical_address;
    u32_dsp sample_count;

    /// ADPCM Predictor (4 bits) and Scale (4 bits)
    union {
        u16_le adpcm_ps;
        BitField<0, 4, u16_le> adpcm_scale;
        BitField<4, 4, u16_le> adpcm_predictor;
    };

    /// ADPCM Historical Samples (y[n-1] and y[n-2])
    u16_le adpcm_yn[2];

    u8 adpcm_flag;

    u8 is_looping;
    u16_le buffer_id;

    INSERT_PADDING_DSPWORDS(1);
};
ASSERT_POD_STRUCT(Buffer, 20);

struct SourceConfiguration {
    union {
        u32_dsp dirty;

        BitField<0 + 16, 1, u32_le> enable_dirty;
        BitField<1 + 16, 1, u32_le> interpolation_dirty;
        BitField<2 + 16, 1, u32_le> rate_multiplier_dirty;
        BitField<3 + 16, 1, u32_le> buffer_queue_dirty;
        BitField<4 + 16, 1, u32_le> loop_related_dirty; ///< TODO: Work out how looped buffers work.
        BitField<5 + 16, 1, u32_le> unknown1_dirty; ///< TODO: Seems to also be set when embedded buffer is updated.
        BitField<6 + 16, 1, u32_le> filters_enabled_dirty;
        BitField<7 + 16, 1, u32_le> simple_filter_dirty;
        BitField<8 + 16, 1, u32_le> biquad_filter_dirty;
        BitField<9 + 16, 1, u32_le> gain_0_dirty;
        BitField<10 + 16, 1, u32_le> gain_1_dirty;
        BitField<11 + 16, 1, u32_le> gain_2_dirty;
        BitField<12 + 16, 1, u32_le> sync_dirty;
        BitField<13 + 16, 1, u32_le> reset_flag;
        //BitField<14 + 16, 1, u32_le> ///< TODO
        BitField<15 + 16, 1, u32_le> embedded_buffer_dirty;
        //BitField<16 - 16, 1, u32_le> flags1_dirty;
        //BitField<17 - 16, 1, u32_le> flags1_dirty; ///< TODO: Why do they sometimes set bit 16 and sometimes bit 17?
        BitField<18 - 16, 1, u32_le> adpcm_coefficients_dirty;
        BitField<19 - 16, 1, u32_le> partial_embedded_buffer_dirty; ///< TODO: Looped buffer related.
        //BitField<20 - 16, 1, u32_le> unknown_weird_state_update_dirty; ///< TODO: Understand what this is actually doing.
    };

    // Gain control

    /// Gain is between 0.0-1.0. How much will this source appear on each of the
    /// 12 channels that feed into the intermediate mixers.
    /// Each of the three intermediate mixers is fed two left and two right channels.
    float gain[3][4];

    // Interpolation

    /// Multiplier for sample rate. Resampling occurs with the selected interpolation method.
    float rate_multiplier;

    enum class InterpolationMode : u8 {
        None = 0,
        Linear = 1,
        Polyphase = 2
    };

    InterpolationMode interpolation_mode;
    INSERT_PADDING_BYTES(1); ///< TODO: Interpolation related

    // Filters

    /// This is the simplest normalized first-order digital recursive filter.
    /// The transfer function of this filter is:
    ///     G(z) = b0 / (1 + a1 z^-1)
    /// Values are signed fixed point with 15 fractional bits.
    struct SimpleFilter {
        s16_le b0;
        s16_le a1;
    };

    /// This is a normalised biquad filter (second-order).
    /// The transfer function of this filter is:
    ///     G(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 - a1 z^-1 - a2 z^-2)
    /// Nintendo chose to negate the feedbackward coefficients. This differs from standard notation
    /// as in: https://ccrma.stanford.edu/~jos/filters/Direct_Form_I.html
    /// Values are signed fixed point with 14 fractional bits.
    struct BiquadFilter {
        s16_le b0;
        s16_le b1;
        s16_le b2;
        s16_le a1;
        s16_le a2;
    };

    union {
        u16_le filters_enabled;
        BitField<0, 1, u16_le> simple_filter_enabled;
        BitField<1, 1, u16_le> biquad_filter_enabled;
    };

    SimpleFilter simple_filter;
    BiquadFilter biquad_filter;

    // Buffer Queue

    u16_le buffers_dirty;             ///< Which of those queued buffers is dirty (bit i == buffers[i])
    Buffer buffers[4];                ///< Queued Buffers

    // Playback controls

    u32_dsp loop_related;
    u8 enable;
    INSERT_PADDING_BYTES(1);
    u16_le sync;                      ///< Application-side sync
    INSERT_PADDING_DSPWORDS(1);
    u16_le unknown1;                  ///< TODO
    INSERT_PADDING_DSPWORDS(2);

    // Embedded Buffer

    u32_dsp physical_address;
    u32_dsp sample_count;

    enum class MonoOrStereo : u16_le {
        Mono = 1,
        Stereo = 2
    };

    enum class Format : u16_le {
        PCM8 = 0,
        PCM16 = 1,
        ADPCM = 2
    };

    union {
        u16_le flags1_raw;
        BitField<0, 2, MonoOrStereo> mono_or_stereo;
        BitField<2, 2, Format> format;
        BitField<5, 1, u16_le> fade_in;
    };

    /// ADPCM Predictor (4 bit) and Scale (4 bit)
    union {
        u16_le adpcm_ps;
        BitField<0, 4, u16_le> adpcm_scale;
        BitField<4, 4, u16_le> adpcm_predictor;
    };
    /// ADPCM Historical Samples (y[n-1] and y[n-2])
    u16_le adpcm_yn[2];

    union {
        u16_le flags2_raw;
        BitField<0, 1, u16_le> adpcm_flag;
        BitField<1, 1, u16_le> is_looping;
    };

    u16_le buffer_id;
};
ASSERT_POD_STRUCT(SourceConfiguration, 192);

struct SourceStatus {
    u8 is_playing;
    u8 buffer_flag;
    u16_le sync;                ///< Synchronises with SourceConfiguration::sync
    u32_dsp buffer_position;    ///< Number of samples into the current buffer
    u16_le current_buffer_id;
    INSERT_PADDING_DSPWORDS(1);
};
ASSERT_POD_STRUCT(SourceStatus, 12);

/// This is delay with feedback.
/// Transfer function:
///     G(z) = a z^-N / (1 - b z^-1 + a g z^-N)
///   where
///     N = frame_count * samples_per_frame
struct DelayEffect {
    union {
        u16_le dirty;
        BitField<0, 1, u16_le> enable_dirty;
        BitField<1, 1, u16_le> work_buffer_address_dirty;
        BitField<2, 1, u16_le> other_dirty; ///< Everything other than enable and work_buffer_address.
    };
    u16_le enable;
    INSERT_PADDING_DSPWORDS(1);
    u16_le outputs;
    u32_dsp work_buffer_address;
    u16_le frame_count;  ///< Frames to delay by
    s16_le g; ///< fixed point
    s16_le a; ///< fixed point
    s16_le b; ///< fixed point
};
ASSERT_POD_STRUCT(DelayEffect, 20);

struct ReverbEffect {
    INSERT_PADDING_DSPWORDS(26); ///< TODO
};
ASSERT_POD_STRUCT(ReverbEffect, 52);

struct DspConfiguration {
    union {
        u32_dsp dirty;

        BitField<0 + 16, 1, u32_le> volume_0_dirty;

        BitField<8 + 16, 1, u32_le> volume_1_dirty;
        BitField<9 + 16, 1, u32_le> volume_2_dirty;
        BitField<10 + 16, 1, u32_le> output_format_dirty;
        BitField<11 + 16, 1, u32_le> limiter_enabled_dirty;
        BitField<12 + 16, 1, u32_le> headphones_connected_dirty;

        BitField<24 - 16, 1, u32_le> mixer1_enabled_dirty;
        BitField<25 - 16, 1, u32_le> mixer2_enabled_dirty;
        BitField<26 - 16, 1, u32_le> delay_effect_0_dirty;
        BitField<27 - 16, 1, u32_le> delay_effect_1_dirty;
        BitField<28 - 16, 1, u32_le> reverb_effect_0_dirty;
        BitField<29 - 16, 1, u32_le> reverb_effect_1_dirty;
    };

    /// The DSP has three audio mixers. This controls the volume level (0.0-1.0) for each.
    float volume[3]; ///< Gains for each of the intermediate mixes at the output mixer.
    INSERT_PADDING_DSPWORDS(2);  ///< TODO
    INSERT_PADDING_DSPWORDS(1);  ///< TODO: Compressor related

    enum class OutputFormat : u16_le {
        Mono = 0,
        Stereo = 1,
        Surround = 2
    };

    OutputFormat output_format;

    u16_le limiter_enabled;      ///< Not sure the exact gain equation for the limiter.
    u16_le headphones_connected; ///< Application updates the DSP on headphone status.
    INSERT_PADDING_DSPWORDS(4);  ///< TODO: Surround sound related
    INSERT_PADDING_DSPWORDS(2);  ///< TODO: Intermediate mixer 1/2 related
    u16_le mixer12_enabled[2];
    DelayEffect delay_effect[2];
    ReverbEffect reverb_effect[2];
    INSERT_PADDING_DSPWORDS(4);
};
ASSERT_POD_STRUCT(DspConfiguration, 0xC4);

// ADPCM Coefficients
struct AdpcmCoefficients {
    /// Coefficients are signed fixed point with 11 fractional bits.
    s16_le coeff[16];
};
ASSERT_POD_STRUCT(AdpcmCoefficients, 32);

struct DspStatus {
    u16_le unknown;
    u16_le dropped_frames;
    INSERT_PADDING_DSPWORDS(0xE);
};
ASSERT_POD_STRUCT(DspStatus, 32);

/// Final mixed output in PCM16 stereo format, what you hear out of the speakers.
/// When the applications writes to this region it has no effect.
struct FinalMixSamples {
    s16_le pcm16[2 * AudioCore::samples_per_frame];
};
ASSERT_POD_STRUCT(FinalMixSamples, 0x280);

/// DSP writes output of intermediate mixers 1 and 2 here.
/// Writes to this region by the application edits the output of the intermediate mixers.
/// This seems to be intended to allow the application to do custom effects on the ARM11.
/// Values that exceed s16 range will be clipped by the DSP after further processing.
struct IntermediateMixSamples {
    s32_le pcm32[4][AudioCore::samples_per_frame];
};

/// Compressor related
struct Compressor {
    INSERT_PADDING_DSPWORDS(0xD20); ///< TODO
};

/// There is no easy way to implement this in a HLE implementation.
struct DspDebug {
    INSERT_PADDING_DSPWORDS(0x130);
};
ASSERT_POD_STRUCT(DspDebug, 0x260);

struct SharedMemory {
    /// Padding
    INSERT_PADDING_DSPWORDS(0x400);

    DspStatus dsp_status;

    DspDebug dsp_debug;

    FinalMixSamples final_samples;

    SourceStatus source_status[AudioCore::num_sources];

    Compressor compressor;

    DspConfiguration dsp_configuration;

    IntermediateMixSamples mix1_samples;
    IntermediateMixSamples mix2_samples;

    SourceConfiguration source_configuration[AudioCore::num_sources];

    AdpcmCoefficients adpcm_coefficients[AudioCore::num_sources];

    /// Unknown 10-14 (Surround sound related)
    INSERT_PADDING_DSPWORDS(0x16ED);

    u16_le frame_counter;
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
