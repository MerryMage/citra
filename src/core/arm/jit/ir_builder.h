// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "core/arm/jit/ir.h"

namespace ArmJit {

class MicroBuilder final {
public:
    explicit MicroBuilder(const LocationDescriptor& desc) : block(desc) {}

    MicroBlock block;

    std::shared_ptr<MicroValue> GetGPR(ArmReg reg);
    std::shared_ptr<MicroValue> ConstU32(u32 value);
    std::shared_ptr<MicroValue> Inst(MicroOp op, std::shared_ptr<MicroValue> a);
    std::shared_ptr<MicroValue> Inst(MicroOp op, std::shared_ptr<MicroValue> a, std::shared_ptr<MicroValue> b);

    static MicroTerminal TermLinkBlock(LocationDescriptor next);
    static MicroTerminal TermLinkBlockFast(LocationDescriptor next);
    static MicroTerminal TermInterpret(LocationDescriptor next);
    static MicroTerminal TermDispatch();
    static MicroTerminal TermIf(Cond cond, MicroTerminal then_, MicroTerminal else_);

    void SetTerm(MicroTerminal term);
};

} // namespace ArmJit
