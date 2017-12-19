// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <string>
#include "common/common_types.h"
#include "common/swap.h"

namespace State {

constexpr u8 MAX_SLOTS = 10;
constexpr u32 SAVE_STATE_VERSION = 1;

struct StateHeader {
    u64 title_id;
    u32 version;
    u32 size;
};

void Init();

/**
 * Called by UI Thread to save the current state as soon as EmuThread::run() is reached
 */
void ScheduleSave(u8 slot);
bool ShouldSave();
void SaveState();

/**
 * Called by UI Thread to load a state as soon as EmuThread::run() is reached
 */
void ScheduleLoad(u8 slot);
bool ShouldLoad();
void LoadState();

void SetTitleID(u64_le id);

} // namespace State
