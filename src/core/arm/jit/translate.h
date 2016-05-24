// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/arm/jit/common.h"
#include "core/arm/jit/ir.h"

namespace ArmJit {

MicroBlock Translate(const LocationDescriptor& location);

} // namespace JitX64