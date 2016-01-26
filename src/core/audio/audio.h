// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace Audio {

void Init();
void Shutdown();

enum class Format : u16 {
    PCM8 = 0,
    PCM16 = 1,
    ADPCM = 2
};

void UpdateFormat(int chanid, int mono_or_stereo, Format format);
void UpdateAdpcm(int chanid, s16 coeffs[16]);

void Play(int channel_id, bool play);

void EnqueueBuffer(int channel_id, u16 buffer_id, const u8* data, int sample_count, bool is_looping);

void Tick(int channel_id);

struct ChannelStatus {
    bool is_enabled;
    bool was_fed_data;   ///< Have we been fed data since being enabled?
    u16 most_recent_buffer_id;
    u32 sample_position; ///< Play position in current buffer
};

ChannelStatus GetStatus(int channel_id);

};
