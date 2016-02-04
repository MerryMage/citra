// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include "common/common_types.h"

namespace DSP {
namespace HLE {

/// Read a DSP pipe.
/// @param pipe_number The Pipe ID
/// @param length How much data to request.
/// @return The data read from the pipe. The size of this vector can be less than the length requested.
std::vector<u8> PipeRead(u32 pipe_number, u32 length);

/// Write to a DSP pipe.
/// @param pipe_number The Pipe ID
/// @param buffer The data to write to the pipe.
void PipeWrite(u32 pipe_number, const std::vector<u8>& buffer);

} // namespace HLE
} // namespace DSP
