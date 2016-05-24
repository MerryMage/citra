// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <tuple>

#include "common/assert.h"
#include "common/common_types.h"

namespace ArmJit {

enum class Cond {
    EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL, NV
};

enum class ArmReg {
    R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15,
    SP = R13,
    LR = R14,
    PC = R15,
    INVALID_REG = 99
};

inline ArmReg operator+ (ArmReg arm_reg, int number) {
    ASSERT(arm_reg != ArmReg::INVALID_REG);

    int value = static_cast<int>(arm_reg) + number;
    ASSERT(value >= 0 && value <= 15);

    return static_cast<ArmReg>(value);
}

struct LocationDescriptor {
    LocationDescriptor(u32 arm_pc, bool TFlag, bool EFlag, Cond cond = Cond::AL)
        : arm_pc(arm_pc), TFlag(TFlag), EFlag(EFlag), cond(cond) {}

    u32 arm_pc;
    bool TFlag; ///< Thumb / ARM
    bool EFlag; ///< Big / Little Endian
    Cond cond;

    bool operator == (const LocationDescriptor& o) const {
        return std::tie(arm_pc, TFlag, EFlag, cond) == std::tie(o.arm_pc, o.TFlag, o.EFlag, o.cond);
    }
};

struct LocationDescriptorHash {
    size_t operator()(const LocationDescriptor& x) const {
        return std::hash<u64>()(static_cast<u64>(x.arm_pc) ^
                                (static_cast<u64>(x.TFlag) << 32) ^
                                (static_cast<u64>(x.EFlag) << 33) ^
                                (static_cast<u64>(x.cond) << 34));
    }
};

} // namespace ArmJit