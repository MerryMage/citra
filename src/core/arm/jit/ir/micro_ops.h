// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <vector>

namespace ArmJit {

/// The operation type of a microinstruction. These are suboperations of a ARM instruction.
enum class MicroOp {
    // Basic load/stores
    ConstU32,          // value := const
    GetGPR,            // value := R[reg]
    SetGPR,            // R[reg] := $0

    // Optimization hints
    PushRSBHint,       // R[14] := $0, and pushes return info onto the return stack buffer [optimization].

    // ARM PC
    AluWritePC,        // R[15] := $0 & (APSR.T ? 0xFFFFFFFE : 0xFFFFFFFC) // ARMv6 behaviour
    LoadWritePC,       // R[15] := $0 & 0xFFFFFFFE, APSR.T := $0 & 0x1     // ARMv6 behaviour (UNPREDICTABLE if $0 & 0x3 == 0)

    // ARM ALU
    Add,               // value := $0 + $1, writes ASPR.NZCV
    AddWithCarry,      // value := $0 + $1 + APSR.C, writes ASPR.NZCV
    Sub,               // value := $0 - $1, writes ASPR.NZCV

    And,               // value := $0 & $1, writes ASPR.NZC
    Eor,               // value := $0 ^ $1, writes ASPR.NZC
    Not,               // value := ~$0

    LSL,               // value := $0 LSL $1, writes ASPR.C
    LSR,               // value := $0 LSR $1, writes ASPR.C
    ASR,               // value := $0 ASR $1, writes ASPR.C
    ROR,               // value := $0 ROR $1, writes ASPR.C
    RRX,               // value := $0 RRX

    CountLeadingZeros, // value := CLZ $0

    // ARM Synchronisation
    ClearExclusive,    // Clears exclusive access record

    // Memory
    Read32,            // value := Memory::Read32($0)
};

/// ARM Flags Bitmap
enum class MicroArmFlags {
    N  = 1 << 0,
    Z  = 1 << 1,
    C  = 1 << 2,
    V  = 1 << 3,
    Q  = 1 << 4,
    GE = 1 << 5,

    None = 0,
    NZC = N | Z | C,
    NZCV = N | Z | C | V,
    Any = N | Z | C | V | Q | GE,
};

MicroArmFlags operator~(MicroArmFlags a);
MicroArmFlags operator|(MicroArmFlags a, MicroArmFlags b);
MicroArmFlags operator&(MicroArmFlags a, MicroArmFlags b);

/// Types of values of micro-instructions
enum class MicroType {
    Void,
    U32,
};

/// Information about an opcode.
struct MicroOpInfo final {
    /// Opcode.
    MicroOp op;
    /// Type of this value.
    MicroType ret_type;
    /// Flags that this micro-instruction reads.
    MicroArmFlags read_flags;
    /// Flags that this micro-instruction can write. (One can restrict this with MicroInst::SetWriteFlags.)
    MicroArmFlags default_write_flags;
    /// Required types of this micro-instruction's arguments.
    std::vector<MicroType> arg_types;
    /// Number of arguments this micro-instruction takes.
    size_t NumArgs() const { return arg_types.size(); }
};

/// Get information about an opcode.
MicroOpInfo GetMicroOpInfo(MicroOp op);

} // namespace ArmJit
