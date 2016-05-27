// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"

#include "core/arm/jit/ir/micro_builder.h"
#include "core/arm/jit/ir/micro_ir.h"
#include "core/arm/jit/ir/micro_ops.h"

namespace ArmJit {

std::shared_ptr<MicroValue> MicroBuilder::GetGPR(ArmReg reg) {
    auto value = std::make_shared<MicroGetGPR>(reg);
    block.instructions.emplace_back(value);
    return value;
}

std::shared_ptr<MicroValue> MicroBuilder::ConstU32(u32 u32_value) {
    auto value = std::make_shared<MicroConstU32>(u32_value);
    block.instructions.emplace_back(value);
    return value;
}

std::shared_ptr<MicroValue> MicroBuilder::SetGPR(ArmReg reg, std::shared_ptr<MicroValue> a) {
    auto value = std::make_shared<MicroSetGPR>(reg);
    value->SetArg(a);
    block.instructions.emplace_back(value);
    return value;
}

std::shared_ptr<MicroValue> MicroBuilder::Inst(MicroOp op, std::shared_ptr<MicroValue> a, MicroArmFlags write_flags) {
    auto value = std::make_shared<MicroInst>(op);
    value->SetArg(0, a);
    value->AssertValid();

    // Ensure we aren't trying to request writes to any flags this instruction cannot write.
    ASSERT((write_flags & ~value->WriteFlags()) == MicroArmFlags::None);
    value->SetWriteFlags(write_flags);
    flags_written = flags_written | write_flags;

    block.instructions.emplace_back(value);
    return value;
}

std::shared_ptr<MicroValue> MicroBuilder::Inst(MicroOp op, std::shared_ptr<MicroValue> a, std::shared_ptr<MicroValue> b, MicroArmFlags write_flags) {
    auto value = std::make_shared<MicroInst>(op);
    value->SetArg(0, a);
    value->SetArg(1, b);
    value->AssertValid();

    // Ensure we aren't trying to request writes to any flags this instruction cannot write.
    ASSERT((write_flags & ~value->WriteFlags()) == MicroArmFlags::None);
    value->SetWriteFlags(write_flags);
    flags_written = flags_written | write_flags;

    block.instructions.emplace_back(value);
    return value;
}

void MicroBuilder::SetTerm(MicroTerminal term) {
    block.terminal = term;
}

} // namespace ArmJit
