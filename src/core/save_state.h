// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <fstream>
#include "common/common_types.h"

namespace State {

constexpr u32 SAVE_STATE_VERSION = 1;

struct StateHeader {
    u32 version;
};

void SaveState(std::ofstream);

enum class LoadStateError {
    None,
    IncorrectVersion,
};

LoadStateError LoadState(std::ifstream);

} // namespace State
