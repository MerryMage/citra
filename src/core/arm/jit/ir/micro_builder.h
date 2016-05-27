// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "core/arm/jit/jit_common.h"
#include "core/arm/jit/ir/micro_ir.h"
#include "core/arm/jit/ir/micro_ops.h"

namespace ArmJit {

/// Convenience class for constructing MicroBlocks.
class MicroBuilder final {
public:
    explicit MicroBuilder(const LocationDescriptor& desc) : block(desc) {}

    MicroBlock block;
    MicroArmFlags flags_written = MicroArmFlags::None;

    std::shared_ptr<MicroValue> GetGPR(ArmReg reg);
    std::shared_ptr<MicroValue> ConstU32(u32 value);
    std::shared_ptr<MicroValue> SetGPR(ArmReg reg, std::shared_ptr<MicroValue> a);
    std::shared_ptr<MicroValue> Inst(MicroOp op, std::shared_ptr<MicroValue> a, MicroArmFlags write_flags = MicroArmFlags::None);
    std::shared_ptr<MicroValue> Inst(MicroOp op, std::shared_ptr<MicroValue> a, std::shared_ptr<MicroValue> b, MicroArmFlags write_flags = MicroArmFlags::None);

    void SetTerm(MicroTerminal term);
};

} // namespace ArmJit
