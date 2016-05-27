// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

namespace ArmJit {
struct LocationDescriptor;
class MicroBlock;
}

namespace ArmJit {

MicroBlock Translate(const LocationDescriptor& location);

} // namespace JitX64