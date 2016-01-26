// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <unordered_map>

#include "common/bit_field.h"
#include "common/logging/log.h"

#include "core/audio/audio.h"
#include "core/core_timing.h"
#include "core/hle/hle.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/dsp_dsp.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Namespace DSP_DSP

namespace DSP_DSP {

struct PairHash {
    template <typename T, typename U>
    std::size_t operator()(const std::pair<T, U> &x) const {
        return std::hash<T>()(x.first) ^ std::hash<U>()(x.second);
    }
};

static u32 read_pipe_count;

static Kernel::SharedPtr<Kernel::Event> semaphore_event;
static u32 semaphore_mask;

static std::unordered_map<std::pair<u32, u32>, Kernel::SharedPtr<Kernel::Event>, PairHash> interrupt_events;

static const u64 frame_tick = 1310252ull;
static int tick_event;

static const int NUM_CHANNELS = 24;

// DSP Addresses

static const VAddr BASE_ADDR_0 = Memory::DSP_RAM_VADDR + 0x40000;
static const VAddr BASE_ADDR_1 = Memory::DSP_RAM_VADDR + 0x60000;

enum DspRegion {
    DSPADDR0 = 0xBFFF, // Frame Counter
    DSPADDR1 = 0x9E92, // Channel Context (x24)
    DSPADDR2 = 0x8680, // Channel Status (x24)
    DSPADDR3 = 0xA792, // ADPCM Coefficients (x24)
    DSPADDR4 = 0x9430, // Context
    DSPADDR5 = 0x8400, // Status
    DSPADDR6 = 0x8540, // Loopback Samples
    DSPADDR7 = 0x9494,
    DSPADDR8 = 0x8710,
    DSPADDR9 = 0x8410, // ???
    DSPADDR10 = 0xA912,
    DSPADDR11 = 0xAA12,
    DSPADDR12 = 0xAAD2,
    DSPADDR13 = 0xAC52,
    DSPADDR14 = 0xAC5C
};

static constexpr VAddr DspAddrToVAddr(VAddr base, DspRegion dsp_addr) {
    return (VAddr(dsp_addr) << 1) + base;
}

/**
 * dsp_u32:
 *     Care must be taken when reading/writing 32-bit values in the DSP shared memory region
 *     as the byte order for 32-bit values is middle endian.
 *     This is presumably because the DSP is big endian with a 16 bit wordsize.
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
        return ((value & 0x0000FFFF) << 16) | ((value & 0xFFFF0000) >> 16);
    }
    u32 storage = 0;
};

#define INSERT_PADDING_DSPWORDS(num_words) u16 CONCAT2(pad, __LINE__)[(num_words)]
#define ASSERT_STRUCT(name, size) \
    static_assert(std::is_standard_layout<name>::value, "Structure doesn't use standard layout"); \
    static_assert(sizeof(name) == (size), "Unexpected struct size")

struct Buffer {
    dsp_u32 physical_address;
    dsp_u32 sample_count;

    INSERT_PADDING_DSPWORDS(3);
    INSERT_PADDING_BYTES(1);

    u8 is_looping;
    u16 buffer_id;

    INSERT_PADDING_DSPWORDS(1);
};

// Userland mainly controls the values in this structure
struct ChannelContext {
    u32 dirty;

    // Effects
    float mix[12];
    float rate;
    u8 rim[2];
    u16 iirfilter_type;
    u16 iirfilter_mono[2];
    u16 iirfilter_biquad[5];

    // Buffer Queue
    u16 buffers_dirty;                //< Which of those queued buffers is dirty (bit i == buffers[i])
    Buffer buffers[4];                //< Queued Buffers

    INSERT_PADDING_DSPWORDS(2);

    u16 is_active;                    //< Lower 8 bits == 0x01 if true.
    u16 sync;

    INSERT_PADDING_DSPWORDS(4);

    // Embedded Buffer
    dsp_u32 physical_address;
    dsp_u32 sample_count;

    union {
        u16 flags1_raw;
        BitField<0, 2, u16> mono_or_stereo;
        BitField<2, 2, Audio::Format> format;
    };

    INSERT_PADDING_DSPWORDS(3);

    union {
        u16 flags2_raw;
        BitField<0, 1, u16> has_adpcm;
        BitField<1, 1, u16> is_looping;
    };

    u16 buffer_id;
};
ASSERT_STRUCT(ChannelContext, 192);

// The DSP controls the values in this structure
struct ChannelStatus {
    u16 is_playing;
    u16 sync;
    dsp_u32 buffer_position;
    u16 current_buffer_id;
    INSERT_PADDING_DSPWORDS(1);
};
ASSERT_STRUCT(ChannelStatus, 12);

struct AdpcmCoefficients {
    s16 coeff[16];
};
ASSERT_STRUCT(AdpcmCoefficients, 32);

// Temporary, switch ChannelContext::dirty to using BitFlags later.
template <size_t bit_number, typename T>
static bool TestAndUnsetBit(T& value) {
    auto& field = *reinterpret_cast<BitField<bit_number, 1, T>*>(&value);
    bool ret = field;
    field = 0;
    return ret;
}

static VAddr GetCurrentBase() {
    // Frame IDs.
    int id0 = (int)Memory::Read16(DspAddrToVAddr(BASE_ADDR_0, DSPADDR0));
    int id1 = (int)Memory::Read16(DspAddrToVAddr(BASE_ADDR_1, DSPADDR0));

    // The frame id increments once per audio frame, with wraparound at 65,535.
    // I am uncertain whether the real DSP actually does something like this,
    // or merely checks for a certan id for wraparound. TODO: Verify.
    if (id1 - id0 > 10000 && id0 < 10) {
        return BASE_ADDR_0;
    } else if (id0 - id1 > 10000 && id1 < 10) {
        return BASE_ADDR_1;
    } else if (id1 > id0) {
        return BASE_ADDR_1;
    } else {
        return BASE_ADDR_0;
    }
}

// Last recorded sync count from ChannelContext.
static std::array<u16, NUM_CHANNELS> syncs;

static ChannelContext GetChannelContext(VAddr base, int channel_id) {
    auto ret = Memory::ExtractFromMemory<ChannelContext>(DspAddrToVAddr(base, DSPADDR1) + channel_id * sizeof(ChannelContext));
    if (!ret) {
        LOG_CRITICAL(Service_DSP, "ExtractFromMemory for DSPADDR1 failed"); // This should never happen.
    }
    return *ret;
}

static void SetChannelContext(VAddr base, int channel_id, const ChannelContext& ctx) {
    if (!Memory::InjectIntoMemory(DspAddrToVAddr(base, DSPADDR1) + channel_id * sizeof(ChannelContext), ctx)) {
        LOG_CRITICAL(Service_DSP, "InjectIntoMemory for DSPADDR1 failed"); // This should never happen.
    }
}

static void ReadChannelContext(VAddr current_base, int channel_id) {
    ChannelContext ctx = GetChannelContext(current_base, channel_id);

    if (!ctx.dirty) {
        return;
    }

    if (TestAndUnsetBit<29>(ctx.dirty)) {
        // First time init
        LOG_DEBUG(Service_DSP, "Channel %i: First Time Init", channel_id);
    }

    if (TestAndUnsetBit<2>(ctx.dirty)) {
        // Update ADPCM coefficients
        auto coeff = Memory::ExtractFromMemory<AdpcmCoefficients>(DspAddrToVAddr(current_base, DSPADDR3) + channel_id * sizeof(AdpcmCoefficients));
        if (!coeff) {
            LOG_CRITICAL(Service_DSP, "ExtractFromMemory for DSPADDR3 failed");
            return;
        }
        Audio::UpdateAdpcm(channel_id, coeff->coeff);
    }

    if (TestAndUnsetBit<17>(ctx.dirty)) {
        // Interpolation type
        LOG_WARNING(Service_DSP, "Channel %i: Unimplemented dirty bit 17", channel_id);
    }

    if (TestAndUnsetBit<18>(ctx.dirty)) {
        // Rate
        LOG_WARNING(Service_DSP, "Channel %i: Unimplemented Rate %f", channel_id, ctx.rate);
    }

    if (TestAndUnsetBit<22>(ctx.dirty)) {
        // IIR
        LOG_WARNING(Service_DSP, "Channel %i: Unimplemented IIR %x", channel_id, ctx.iirfilter_type);
    }

    if (TestAndUnsetBit<28>(ctx.dirty)) {
        // Sync count
        LOG_DEBUG(Service_DSP, "Channel %i: Update Sync Count");
        syncs[channel_id] = ctx.sync;
    }

    if (TestAndUnsetBit<25>(ctx.dirty) | TestAndUnsetBit<26>(ctx.dirty) | TestAndUnsetBit<27>(ctx.dirty)) {
        // Mix
        for (int i = 0; i < 12; i++)
            LOG_DEBUG(Service_DSP, "Channel %i: mix[%i] %f", channel_id, i, ctx.mix[i]);
    }

    if (TestAndUnsetBit<4>(ctx.dirty) | TestAndUnsetBit<21>(ctx.dirty) | TestAndUnsetBit<30>(ctx.dirty)) {
        // TODO(merry): One of these bits might merely signify an update to the format. Verify this.

        // Format updated
        Audio::UpdateFormat(channel_id, ctx.mono_or_stereo, ctx.format);

        // Synchronise flags
        /*
        auto ctx0 = GetChannelContext(BASE_ADDR_0, channel_id);
        auto ctx1 = GetChannelContext(BASE_ADDR_1, channel_id);
        ctx0.flags1_raw = ctx1.flags1_raw = ctx.flags1_raw;
        ctx0.flags2_raw = ctx1.flags2_raw = ctx.flags2_raw;
        SetChannelContext(BASE_ADDR_0, channel_id, ctx0);
        SetChannelContext(BASE_ADDR_1, channel_id, ctx1);
        */

        // Embedded Buffer Changed
        Audio::EnqueueBuffer(channel_id, ctx.buffer_id, Memory::GetPhysicalPointer(ctx.physical_address), ctx.sample_count, ctx.is_looping);
    }

    if (TestAndUnsetBit<19>(ctx.dirty)) {
        // Buffer queue
        for (int i = 0; i < 4; i++) {
            if (ctx.buffers_dirty & (1 << i)) {
                const auto& b = ctx.buffers[i];
                Audio::EnqueueBuffer(channel_id, b.buffer_id, Memory::GetPhysicalPointer(b.physical_address), b.sample_count, b.is_looping);
            }
        }

        if (ctx.buffers_dirty & ~0xFUL) {
            LOG_ERROR(Service_DSP, "Channel %i: Unknown channel buffer dirty bits: 0x%04x", channel_id, ctx.buffers_dirty);
        }

        ctx.buffers_dirty = 0;
    }

    if (TestAndUnsetBit<16>(ctx.dirty)) {
        // Is Active?
        Audio::Play(channel_id, (ctx.is_active & 0xFF) != 0);
    }

    if (ctx.dirty) {
        LOG_ERROR(Service_DSP, "Channel %i: Unknown channel dirty bits: 0x%08x", channel_id, ctx.dirty);
    }

    ctx.dirty = 0;
    SetChannelContext(current_base, channel_id, ctx);
}

static void UpdateChannelStatus(int channel_id) {
    auto audio_status = Audio::GetStatus(channel_id);

    ChannelStatus status;
    status.sync = syncs[channel_id];
    status.current_buffer_id = audio_status.most_recent_buffer_id;
    status.buffer_position = audio_status.sample_position;
    status.is_playing = 0;
    if (audio_status.is_enabled) status.is_playing |= 1;
    if (audio_status.was_fed_data) status.is_playing |= 0x100;

    bool success = true;
    success &= Memory::InjectIntoMemory(DspAddrToVAddr(BASE_ADDR_0, DSPADDR2) + channel_id * sizeof(ChannelStatus), status);
    success &= Memory::InjectIntoMemory(DspAddrToVAddr(BASE_ADDR_1, DSPADDR2) + channel_id * sizeof(ChannelStatus), status);
    if (!success) {
        LOG_CRITICAL(Service_DSP, "InjectIntoMemory for DSPADDR2 failed");
    }
}

static void AudioTick(u64, int cycles_late) {
    VAddr current_base = GetCurrentBase();

    for (int channel_id = 0; channel_id < NUM_CHANNELS; channel_id++) {
        ReadChannelContext(current_base, channel_id);

        Audio::Tick(channel_id);

        UpdateChannelStatus(channel_id);
    }

    for (auto interrupt_event : interrupt_events)
        interrupt_event.second->Signal();

    CoreTiming::ScheduleEvent(frame_tick-cycles_late, tick_event, 0);
}

/**
 * DSP_DSP::ConvertProcessAddressFromDspDram service function
 *  Inputs:
 *      1 : Address
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : (inaddr << 1) + 0x1FF40000 (where 0x1FF00000 is the DSP RAM address)
 */
static void ConvertProcessAddressFromDspDram(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 addr = cmd_buff[1];

    cmd_buff[1] = 0; // No error
    cmd_buff[2] = DspAddrToVAddr(BASE_ADDR_0, (DspRegion)addr);
}

/**
 * DSP_DSP::LoadComponent service function
 *  Inputs:
 *      1 : Size
 *      2 : Unknown (observed only half word used)
 *      3 : Unknown (observed only half word used)
 *      4 : (size << 4) | 0xA
 *      5 : Buffer address
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : Component loaded, 0 on not loaded, 1 on loaded
 */
static void LoadComponent(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 size       = cmd_buff[1];
    u32 unk1       = cmd_buff[2];
    u32 unk2       = cmd_buff[3];
    u32 new_size   = cmd_buff[4];
    u32 buffer     = cmd_buff[5];

    cmd_buff[1] = 0; // No error
    cmd_buff[2] = 1; // Pretend that we actually loaded the DSP firmware

    // TODO(bunnei): Implement real DSP firmware loading

    LOG_WARNING(Service_DSP, "(STUBBED) called size=0x%X, unk1=0x%08X, unk2=0x%08X, new_size=0x%X, buffer=0x%08X",
                size, unk1, unk2, new_size, buffer);
}

/**
 * DSP_DSP::GetSemaphoreEventHandle service function
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      3 : Semaphore event handle
 */
static void GetSemaphoreEventHandle(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error
    cmd_buff[3] = Kernel::g_handle_table.Create(semaphore_event).MoveFrom(); // Event handle

    LOG_WARNING(Service_DSP, "(STUBBED) called");
}

/**
 * DSP_DSP::FlushDataCache service function
 *
 * This Function is a no-op, We aren't emulating the CPU cache any time soon.
 *
 *  Inputs:
 *      1 : Address
 *      2 : Size
 *      3 : Value 0, some descriptor for the KProcess Handle
 *      4 : KProcess handle
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 */
static void FlushDataCache(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();
    u32 address = cmd_buff[1];
    u32 size    = cmd_buff[2];
    u32 process = cmd_buff[4];

    // TODO(purpasmart96): Verify return header on HW

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error

    LOG_DEBUG(Service_DSP, "(STUBBED) called address=0x%08X, size=0x%X, process=0x%08X",
              address, size, process);
}

/**
 * DSP_DSP::RegisterInterruptEvents service function
 *  Inputs:
 *      1 : Interrupt
 *      2 : Number
 *      4 : Interrupt event handle
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 */
static void RegisterInterruptEvents(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 interrupt = cmd_buff[1]; // TODO(merry): Confirm the purpose of each interrupt. Presumably there would be one interrupt that would allow for ARM11 modification of the output.
    u32 number = cmd_buff[2];
    u32 event_handle = cmd_buff[4];

    if (!event_handle) {
        // Unregister the event for this interrupt and number
        interrupt_events.erase(std::make_pair(interrupt, number));
        cmd_buff[1] = RESULT_SUCCESS.raw;
    } else {
        auto evt = Kernel::g_handle_table.Get<Kernel::Event>(event_handle);
        if (evt != nullptr) {
            interrupt_events[std::make_pair(interrupt, number)] = evt;
            cmd_buff[1] = RESULT_SUCCESS.raw; // No error
        } else {
            LOG_ERROR(Service_DSP, "called with invalid handle=%08X", event_handle);

            // TODO(yuriks): An error should be returned from SendSyncRequest, not in the cmdbuf
            cmd_buff[1] = -1;
        }
    }

    LOG_WARNING(Service_DSP, "(STUBBED) called interrupt=%u, number=%u, event_handle=0x%08X", interrupt, number, event_handle);
}

/**
 * DSP_DSP::SetSemaphore service function
 *  Inputs:
 *      1 : Unknown (observed only half word used)
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *  Notes:
 *      Games do not seem to rely on the DSP semaphore very much
 */
static void SetSemaphore(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    cmd_buff[1] = 0; // No error

    LOG_WARNING(Service_DSP, "(STUBBED) called");
}

/**
 * DSP_DSP::WriteProcessPipe service function
 *  Inputs:
 *      1 : Number
 *      2 : Size
 *      3 : (size <<14) | 0x402
 *      4 : Buffer
 *  Outputs:
 *      0 : Return header
 *      1 : Result of function, 0 on success, otherwise error code
 */
static void WriteProcessPipe(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 number   = cmd_buff[1];
    u32 size     = cmd_buff[2];
    u32 new_size = cmd_buff[3];
    u32 buffer   = cmd_buff[4];

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error

    LOG_WARNING(Service_DSP, "(STUBBED) called number=%u, size=0x%X, new_size=0x%X, buffer=0x%08X",
                number, size, new_size, buffer);
}

/**
 * DSP_DSP::ReadPipeIfPossible service function
 *  Inputs:
 *      1 : Unknown
 *      2 : Unknown
 *      3 : Size in bytes of read (observed only lower half word used)
 *      0x41 : Virtual address to read from DSP pipe to in memory
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : Number of bytes read from pipe
 */
static void ReadPipeIfPossible(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 pipe = cmd_buff[1];
    u32 unk2 = cmd_buff[2];
    u32 size = cmd_buff[3] & 0xFFFF;// Lower 16 bits are size
    VAddr addr = cmd_buff[0x41];

    if (pipe != 2) {
        LOG_ERROR(Service_DSP, "I'm not sure what to do when pipe=0x%08x\n", pipe);
    }

    // Canned DSP responses that games expect. These were taken from HW by 3dmoo team.
    // TODO: Remove this hack :)
    // FIXME(merry): Incorrect behaviour; the read buffer isn't a single stream, nor does it behave like a stream.
    static const std::array<u16, 16> canned_read_pipe = {{
        0x000F,
        DSPADDR0,
        DSPADDR1,
        DSPADDR2,
        DSPADDR3,
        DSPADDR4,
        DSPADDR5,
        DSPADDR6,
        DSPADDR7,
        DSPADDR8,
        DSPADDR9,
        DSPADDR10,
        DSPADDR11,
        DSPADDR12,
        DSPADDR13,
        DSPADDR14,
    }};

    u32 initial_size = read_pipe_count;

    for (unsigned offset = 0; offset < size; offset += sizeof(u16)) {
        if (read_pipe_count < canned_read_pipe.size()) {
            Memory::Write16(addr + offset, canned_read_pipe[read_pipe_count]);
            read_pipe_count++;
        } else {
            LOG_ERROR(Service_DSP, "canned read pipe log exceeded!");
            break;
        }
    }

    cmd_buff[1] = 0; // No error
    cmd_buff[2] = (read_pipe_count - initial_size) * sizeof(u16);

    LOG_WARNING(Service_DSP, "(STUBBED) called pipe=0x%08X, unk2=0x%08X, size=0x%X, buffer=0x%08X",
                pipe, unk2, size, addr);
}

/**
 * DSP_DSP::SetSemaphoreMask service function
 *  Inputs:
 *      1 : Mask
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 */
static void SetSemaphoreMask(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 mask = cmd_buff[1];

    semaphore_mask = mask;

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error

    LOG_WARNING(Service_DSP, "(STUBBED) called mask=0x%08X", mask);
}

/**
 * DSP_DSP::GetHeadphoneStatus service function
 *  Inputs:
 *      1 : None
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : The headphone status response, 0 = Not using headphones?,
 *          1 = using headphones?
 */
static void GetHeadphoneStatus(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error
    cmd_buff[2] = 0; // Not using headphones?

    LOG_DEBUG(Service_DSP, "(STUBBED) called");
}

/**
* DSP_DSP::RecvData service function
*  Inputs:
*      1 : Register Number
*  Outputs:
*      1 : Result of function, 0 on success, otherwise error code
*      2 : Value in the register
*/
static void RecvData(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 register_number = cmd_buff[1];

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error
    cmd_buff[2] = 1;

    LOG_WARNING(Service_DSP, "(STUBBED) called register=%u", register_number);
}

/**
* DSP_DSP::RecvDataIsReady service function
*  Inputs:
*      1 : Register Number
*  Outputs:
*      1 : Result of function, 0 on success, otherwise error code
*      2 : non-zero == ready
*  Notes:
*      Seems to be mainly called when going into sleep mode.
*/
static void RecvDataIsReady(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 register_number = cmd_buff[1];

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error
    cmd_buff[2] = 1;

    LOG_WARNING(Service_DSP, "(STUBBED) called register=%u", register_number);
}

const Interface::FunctionInfo FunctionTable[] = {
    {0x00010040, RecvData,                         "RecvData"},
    {0x00020040, RecvDataIsReady,                  "RecvDataIsReady"},
    {0x00030080, nullptr,                          "SendData"},
    {0x00040040, nullptr,                          "SendDataIsEmpty"},
    {0x000500C2, nullptr,                          "SendFifoEx"},
    {0x000600C0, nullptr,                          "RecvFifoEx"},
    {0x00070040, SetSemaphore,                     "SetSemaphore"},
    {0x00080000, nullptr,                          "GetSemaphore"},
    {0x00090040, nullptr,                          "ClearSemaphore"},
    {0x000A0040, nullptr,                          "MaskSemaphore"},
    {0x000B0000, nullptr,                          "CheckSemaphoreRequest"},
    {0x000C0040, ConvertProcessAddressFromDspDram, "ConvertProcessAddressFromDspDram"},
    {0x000D0082, WriteProcessPipe,                 "WriteProcessPipe"},
    {0x000E00C0, nullptr,                          "ReadPipe"},
    {0x000F0080, nullptr,                          "GetPipeReadableSize"},
    {0x001000C0, ReadPipeIfPossible,               "ReadPipeIfPossible"},
    {0x001100C2, LoadComponent,                    "LoadComponent"},
    {0x00120000, nullptr,                          "UnloadComponent"},
    {0x00130082, FlushDataCache,                   "FlushDataCache"},
    {0x00140082, nullptr,                          "InvalidateDCache"},
    {0x00150082, RegisterInterruptEvents,          "RegisterInterruptEvents"},
    {0x00160000, GetSemaphoreEventHandle,          "GetSemaphoreEventHandle"},
    {0x00170040, SetSemaphoreMask,                 "SetSemaphoreMask"},
    {0x00180040, nullptr,                          "GetPhysicalAddress"},
    {0x00190040, nullptr,                          "GetVirtualAddress"},
    {0x001A0042, nullptr,                          "SetIirFilterI2S1_cmd1"},
    {0x001B0042, nullptr,                          "SetIirFilterI2S1_cmd2"},
    {0x001C0082, nullptr,                          "SetIirFilterEQ"},
    {0x001D00C0, nullptr,                          "ReadMultiEx_SPI2"},
    {0x001E00C2, nullptr,                          "WriteMultiEx_SPI2"},
    {0x001F0000, GetHeadphoneStatus,               "GetHeadphoneStatus"},
    {0x00200040, nullptr,                          "ForceHeadphoneOut"},
    {0x00210000, nullptr,                          "GetIsDspOccupied"},
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Interface class

Interface::Interface() {
    semaphore_event = Kernel::Event::Create(RESETTYPE_ONESHOT, "DSP_DSP::semaphore_event");
    interrupt_events.clear();
    read_pipe_count = 0;

    Register(FunctionTable);

    tick_event = CoreTiming::RegisterEvent("DSP_DSP::tick_event", AudioTick);
    CoreTiming::ScheduleEvent(frame_tick, tick_event, 0);
}

Interface::~Interface() {
    semaphore_event = nullptr;
    interrupt_events.clear();

    CoreTiming::UnscheduleEvent(tick_event, 0);
}

} // namespace
