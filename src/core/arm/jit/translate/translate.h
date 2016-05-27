// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

namespace ArmJit {
struct LocationDescriptor;
class MicroBlock;
}

namespace ArmJit {

/**
 * This function takes a LocationDescriptor which describes the location of a basic block.
 * It then translates those ARM or Thumb instructions (according to location.TFlag) into
 * our platform-agnostic intermediate representation.
 *
 * This ensures the bulk of our ARM logic is portable cross-platform and also allows for
 * cross-platform optimizations to be made by modifying the IR.
 */
MicroBlock Translate(const LocationDescriptor& location);

} // namespace JitX64
