// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"

#include "core/arm/jit/ir_builder.h"

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

std::shared_ptr<MicroValue> MicroBuilder::Inst(MicroOp op, std::shared_ptr<MicroValue> a, MicroArmFlags write_flags) {
    auto value = MicroInst::Build(op, { a });

    // Ensure we aren't trying to write to any flags this instruction can never write.
    ASSERT((write_flags & ~value->WriteFlags()) == MicroArmFlags::None);
    value->SetWriteFlags(write_flags);

    block.instructions.emplace_back(value);
    return value;
}

std::shared_ptr<MicroValue> MicroBuilder::Inst(MicroOp op, std::shared_ptr<MicroValue> a, std::shared_ptr<MicroValue> b, MicroArmFlags write_flags) {
    auto value = MicroInst::Build(op, { a, b });

    // Ensure we aren't trying to write to any flags this instruction can never write.
    ASSERT((write_flags & ~value->WriteFlags()) == MicroArmFlags::None);
    value->SetWriteFlags(write_flags);

    block.instructions.emplace_back(value);
    return value;
}

MicroTerminal MicroBuilder::TermLinkBlock(LocationDescriptor next) {
    return MicroTerminal(MicroTerm::LinkBlock({ next }));
}

MicroTerminal MicroBuilder::TermLinkBlockFast(LocationDescriptor next) {
    return MicroTerminal(MicroTerm::LinkBlockFast({ next }));
}

MicroTerminal MicroBuilder::TermInterpret(LocationDescriptor next) {
    return MicroTerminal(MicroTerm::Interpret({ next }));
}

MicroTerminal MicroBuilder::TermDispatch() {
    return MicroTerminal(MicroTerm::ReturnToDispatch());
}

MicroTerminal MicroBuilder::TermIf(Cond cond, MicroTerminal then_, MicroTerminal else_) {
    return MicroTerminal(MicroTerm::If({ cond, then_, else_ }));
}

void MicroBuilder::SetTerm(MicroTerminal term) {
    block.terminal = term;
}

} // namespace ArmJit
