// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>

#include <boost/optional.hpp>

#if 0

#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"

#include "core/arm/decoder/decoder.h"

namespace ArmDecoder {

ThumbMatcher MakeMatcher(const char* const str, std::function<void(Visitor* v, u16 instruction)> fn) {
    ASSERT(strlen(str) == 16);

    u16 mask = 0;
    u16 expect = 0;

    for (size_t i = 0; i < 16; i++) {
        const size_t bit_position = 15 - i;
        const u16 bit = 1 << bit_position;

        switch (str[i]) {
        case '0':
            mask |= bit;
            break;
        case '1':
            mask |= bit;
            expect |= bit;
            break;
        default:
            break;
        }
    }

    return { mask, expect, fn };
}

using BitUtil::Bit;
using BitUtil::Bits;

static const std::array<ThumbInstruction, 27> thumb_instruction_table = { {
    { "LSL/LSR/ASR",             MakeMatcher("000ooxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opcode = Bits<11, 12>(instruction);
        u32 imm5 = Bits<6, 10>(instruction);
        Register Rm = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opcode) {
        case 0: // LSL <Rd>, <Rm>, #<imm5>
            v->MOV_reg(Cond::AL, /*S=*/true, Rd, imm5, ShiftType::LSL, Rm);
            break;
        case 1: // LSR <Rd>, <Rm>, #<imm5>
            v->MOV_reg(Cond::AL, /*S=*/true, Rd, imm5, ShiftType::LSR, Rm);
            break;
        case 2: // ASR <Rd>, <Rm>, #<imm5>
            v->MOV_reg(Cond::AL, /*S=*/true, Rd, imm5, ShiftType::ASR, Rm);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "ADD/SUB_reg",              MakeMatcher("000110oxxxxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opcode = Bit<9>(instruction);
        Register Rm = static_cast<Register>(Bits<6, 8>(instruction));
        Register Rn = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opcode) {
        case 0: // ADD <Rd>, <Rn>, <Rm>
            v->ADD_reg(Cond::AL, /*S=*/true, Rn, Rd, 0, ShiftType::LSL, Rm);
            break;
        case 1: // SUB <Rd>, <Rn>, <Rm>
            v->SUB_reg(Cond::AL, /*S=*/true, Rn, Rd, 0, ShiftType::LSL, Rm);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "ADD/SUB_imm",             MakeMatcher("000111oxxxxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opcode = Bit<9>(instruction);
        u32 imm3 = Bits<6, 8>(instruction);
        Register Rn = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opcode) {
        case 0: // ADD <Rd>, <Rn>, #<imm3>
            v->ADD_imm(Cond::AL, /*S=*/true, Rn, Rd, 0, imm3);
            break;
        case 1: // SUB <Rd>, <Rn>, #<imm3>
            v->SUB_imm(Cond::AL, /*S=*/true, Rn, Rd, 0, imm3);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "add/sub/cmp/mov_imm",     MakeMatcher("001ooxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opcode = Bits<11, 12>(instruction);
        Register Rd = static_cast<Register>(Bits<8, 10>(instruction));
        u32 imm8 = Bits<0, 7>(instruction);
        switch (opcode) {
        case 0: // MOV Rd, #imm8
            v->MOV_imm(Cond::AL, /*S=*/true, Rd, 0, imm8);
            break;
        case 1: // CMP Rn, #imm8
            v->CMP_imm(Cond::AL, Rd, 0, imm8);
            break;
        case 2: // ADD Rd, #imm8
            v->ADD_imm(Cond::AL, /*S=*/true, Rd, Rd, 0, imm8);
            break;
        case 3: // SUB Rd, #imm8
            v->SUB_imm(Cond::AL, /*S=*/true, Rd, Rd, 0, imm8);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "data processing reg",     MakeMatcher("010000ooooxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opcode = Bits<6, 9>(instruction);
        Register Ra = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rb = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opcode) {
        case 0: // AND Rd, Rm
            v->AND_reg(Cond::AL, /*S=*/true, Rb, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 1: // EOR Rd, Rm
            v->EOR_reg(Cond::AL, /*S=*/true, Rb, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 2: // LSL Rd, Rs
            v->MOV_rsr(Cond::AL, /*S=*/true, Rb, Ra, ShiftType::LSL, Rb);
            break;
        case 3: // LSR Rd, Rs
            v->MOV_rsr(Cond::AL, /*S=*/true, Rb, Ra, ShiftType::LSR, Rb);
            break;
        case 4: // ASR Rd, Rs
            v->MOV_rsr(Cond::AL, /*S=*/true, Rb, Ra, ShiftType::ASR, Rb);
            break;
        case 5: // ADC Rd, Rm
            v->ADC_reg(Cond::AL, /*S=*/true, Rb, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 6: // SBC Rd, Rm
            v->SBC_reg(Cond::AL, /*S=*/true, Rb, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 7: // ROR Rd, Rs
            v->MOV_rsr(Cond::AL, /*S=*/true, Rb, Ra, ShiftType::ROR, Rb);
            break;
        case 8: // TST Rm, Rn
            v->TST_reg(Cond::AL, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 9: // NEG Rd, Rm
            v->RSB_imm(Cond::AL, /*S=*/true, Ra, Rb, 0, 0);
            break;
        case 10: // CMP Rm, Rn
            v->CMP_reg(Cond::AL, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 11: // CMN Rm, Rn
            v->CMN_reg(Cond::AL, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 12: // ORR Rd, Rm
            v->ORR_reg(Cond::AL, /*S=*/true, Rb, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 13: // MUL Rd, Rm
            v->MUL(Cond::AL, /*S=*/true, Rb, Rb, Ra);
            break;
        case 14: // BIC Rm, Rd
            v->BIC_reg(Cond::AL, /*S=*/true, Rb, Rb, 0, ShiftType::LSL, Ra);
            break;
        case 15: // MVN Rd, Rm
            v->MVN_reg(Cond::AL, /*S=*/true, Rb, 0, ShiftType::LSL, Ra);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "special data processing", MakeMatcher("010001ooxxxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opcode = Bits<8, 9>(instruction);
        Register Rm = static_cast<Register>(Bits<3, 6>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction) | (Bit<7>(instruction) << 3));
        switch (opcode) {
        case 0: // ADD Rd, Rm
            v->ADD_reg(Cond::AL, /*S=*/false, Rd, Rd, 0, ShiftType::LSL, Rm);
            break;
        case 1: // CMP Rm, Rn
            v->CMP_reg(Cond::AL, Rd, 0, ShiftType::LSL, Rm);
            break;
        case 2: // MOV Rd, Rm
            v->MOV_reg(Cond::AL, /*S=*/false, Rd, 0, ShiftType::LSL, Rm);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "BLX/BX",                  MakeMatcher("01000111xxxxx000", [](Visitor* v, u16 instruction) {
        bool L = Bit<7>(instruction);
        Register Rm = static_cast<Register>(Bits<3, 6>(instruction));
        if (!L) { // BX Rm
            v->BX(Cond::AL, Rm);
        } else { // BLX Rm
            v->BLX_reg(Cond::AL, Rm);
        }
    })},
    { "load from literal pool",  MakeMatcher("01001xxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        // LDR Rd, [PC, #]
        Register Rd = static_cast<Register>(Bits<8, 10>(instruction));
        u32 imm8 = Bits<0, 7>(instruction);
        v->LDR_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Register::PC, Rd, imm8 * 4);
    })},
    { "load/store reg offset",   MakeMatcher("0101oooxxxxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opcode = Bits<9, 11>(instruction);
        Register Rm = static_cast<Register>(Bits<6, 8>(instruction));
        Register Rn = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opcode) {
        case 0: // STR Rd, [Rn, Rm]
            v->STR_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, 0, ShiftType::LSL, Rm);
            break;
        case 1: // STRH Rd, [Rn, Rm]
            v->STRH_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, Rm);
            break;
        case 2: // STRB Rd, [Rn, Rm]
            v->STRB_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, 0, ShiftType::LSL, Rm);
            break;
        case 3: // LDRSB Rd, [Rn, Rm]
            v->LDRSB_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, Rm);
            break;
        case 4: // LDR Rd, [Rn, Rm]
            v->LDR_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, 0, ShiftType::LSL, Rm);
            break;
        case 5: // LDRH Rd, [Rn, Rm]
            v->LDRH_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, Rm);
            break;
        case 6: // LDRB Rd, [Rn, Rm]
            v->LDRB_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, 0, ShiftType::LSL, Rm);
            break;
        case 7: // LDRSH Rd, [Rn, Rm]
            v->LDRSH_reg(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, Rm);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "STR(B)/LDR(B)_imm",       MakeMatcher("011xxxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opc = Bits<11, 12>(instruction);
        u32 offset = Bits<6, 10>(instruction);
        Register Rn = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opc) {
        case 0: // STR Rd, [Rn, #offset]
            v->STR_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, offset * 4);
            break;
        case 1: // LDR Rd, [Rn, #offset]
            v->LDR_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, offset * 4);
            break;
        case 2: // STRB Rd, [Rn, #offset]
            v->STRB_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, offset);
            break;
        case 3: // LDRB Rd, [Rn, #offset]
            v->LDRB_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, offset);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "STRH/LDRH_imm",           MakeMatcher("1000xxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        bool L = Bit<11>(instruction);
        u32 offset = Bits<6, 10>(instruction);
        Register Rn = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        if (!L) { // STRH Rd, [Rn, #offset]
            v->STRH_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, (offset * 2) >> 4, (offset * 2) & 0xF);
        } else { // LDRH Rd, [Rn, #offset]
            v->LDRH_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Rn, Rd, (offset * 2) >> 4, (offset * 2) & 0xF);
        }
    })},
    { "load/store stack",        MakeMatcher("1001xxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        bool L = Bit<11>(instruction);
        Register Rd = static_cast<Register>(Bits<8, 10>(instruction));
        u32 offset = Bits<0, 7>(instruction);
        if (!L) { // STR Rd, [SP, #offset]
            v->STR_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Register::SP, Rd, offset * 4);
        } else { // LDR Rd, [SP, #offset]
            v->LDR_imm(Cond::AL, /*P=*/1, /*U=*/1, /*W=*/0, Register::SP, Rd, offset * 4);
        }
    })},
    { "add to sp/pc",            MakeMatcher("1010oxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        // ADD Rd, PC/SP, #imm8
        Register Rn = Bit<11>(instruction) ? Register::SP : Register::PC;
        Register Rd = static_cast<Register>(Bits<8, 10>(instruction));
        u32 imm8 = Bits<0, 7>(instruction);
        // TODO: ADR implementation incorrect. Reimplement properly.
        v->ADD_imm(Cond::AL, /*S=*/false, Rn, Rd, 0xF, imm8);
    })},
    { "adjust stack ptr",        MakeMatcher("10110000oxxxxxxx", [](Visitor* v, u16 instruction) {
        // SUB SP, SP, #<imm7*4>
        u32 opc = Bit<7>(instruction);
        u32 imm7 = Bits<0, 6>(instruction);
        switch (opc) {
        case 0:
            v->ADD_imm(Cond::AL, /*S=*/false, Register::SP, Register::SP, 0xF, imm7);
            break;
        case 1:
            v->SUB_imm(Cond::AL, /*S=*/false, Register::SP, Register::SP, 0xF, imm7);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "sign/zero extend",        MakeMatcher("10110010ooxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opc = Bits<6, 7>(instruction);
        Register Rm = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opc) {
        case 0: // SXTH Rd, Rm
            v->SXTH(Cond::AL, Rd, SignExtendRotation::ROR_0, Rm);
            break;
        case 1: // SXTB Rd, Rm
            v->SXTB(Cond::AL, Rd, SignExtendRotation::ROR_0, Rm);
            break;
        case 2: // UXTH Rd, Rm
            v->UXTH(Cond::AL, Rd, SignExtendRotation::ROR_0, Rm);
            break;
        case 3: // UXTB Rd, Rm
            v->UXTB(Cond::AL, Rd, SignExtendRotation::ROR_0, Rm);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "PUSH/POP_reglist",        MakeMatcher("1011x10xxxxxxxxx", [](Visitor* v, u16 instruction) {
        bool L = Bit<11>(instruction);
        u32 R = Bit<8>(instruction);
        u32 reglist = Bits<0, 7>(instruction);
        if (!L) { // PUSH {reglist, <R>=LR}
            reglist |= R << 14;
            // Equivalent to STMDB SP!, {reglist}
            v->STM(Cond::AL, /*P=*/1, /*U=*/0, /*W=*/1, Register::SP, reglist);
        } else { // POP {reglist, <R>=PC}
            reglist |= R << 15;
            // Equivalent to LDMIA SP!, {reglist}
            v->LDM(Cond::AL, /*P=*/0, /*U=*/1, /*W=*/1, Register::SP, reglist);
        }
    })},
    { "SETEND",                  MakeMatcher("101101100101x000", [](Visitor* v, u16 instruction) {
        u16 E = Bit<3>(instruction);
        v->SETEND(E);
    })},
    { "change processor state",  MakeMatcher("10110110011x0xxx", [](Visitor* v, u16 instruction) {
        u16 imod = Bit<4>(instruction);
        u16 A = Bit<2>(instruction);
        u16 I = Bit<1>(instruction);
        u16 F = Bit<0>(instruction);
        v->CPS();
    })},
    { "reverse bytes",           MakeMatcher("10111010ooxxxxxx", [](Visitor* v, u16 instruction) {
        u32 opc = Bits<6, 7>(instruction);
        Register Rn = static_cast<Register>(Bits<3, 5>(instruction));
        Register Rd = static_cast<Register>(Bits<0, 2>(instruction));
        switch (opc) {
        case 0: // REV Rd, Rn
            v->REV(Cond::AL, Rd, Rn);
            break;
        case 1: // REV16 Rd, Rn
            v->REV16(Cond::AL, Rd, Rn);
            break;
        case 2: // undefined
            v->UDF();
            break;
        case 3: // REVSH Rd, Rn
            v->REVSH(Cond::AL, Rd, Rn);
            break;
        default:
            UNREACHABLE();
        }
    })},
    { "BKPT",                    MakeMatcher("10111110xxxxxxxx", [](Visitor* v, u16 instruction) {
        // BKPT #imm8
        Imm8 imm8 = Bits<0, 7>(instruction);
        v->BKPT(Cond::AL, imm8 >> 4, imm8 & 0xF);
    })},
    { "STMIA/LDMIA",             MakeMatcher("1100xxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        bool L = Bit<11>(instruction);
        Register Rn = static_cast<Register>(Bits<8, 10>(instruction));
        u32 reglist = Bits<0, 7>(instruction);
        if (!L) { // STMIA Rn!, { reglist }
            v->STM(Cond::AL, /*P=*/0, /*U=*/1, /*W=*/1, Rn, reglist);
        } else { // LDMIA Rn!, { reglist }
            RegisterList Rn_bit = 1 << static_cast<unsigned>(Rn);
            bool w = (reglist & Rn_bit) == 0;
            v->LDM(Cond::AL, /*P=*/0, /*U=*/1, /*W=*/w, Rn, reglist);
        }
    })},
    { "B<cond>",                 MakeMatcher("1101xxxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        // B<cond> <PC + #offset*2>
        Cond cond = static_cast<Cond>(Bits<8, 11>(instruction));
        s32 offset = Bits<0, 7>(instruction);
        ASSERT_MSG(cond != Cond::AL, "UNDEFINED");
        v->thumb_B(cond, offset);
    })},
    { "SWI",                     MakeMatcher("11011111xxxxxxxx", [](Visitor* v, u16 instruction) {
        // SWI #imm8
        Imm8 imm8 = Bits<0, 7>(instruction);
        v->SVC(Cond::AL, imm8);
    })},
    { "B",                       MakeMatcher("11100xxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        // B <PC + #offset*2>
        Imm11 imm11 = Bits<0, 10>(instruction);
        v->thumb_B(imm11);
    })},
    { "BLX (suffix)",            MakeMatcher("11101xxxxxxxxxx0", [](Visitor* v, u16 instruction) {
        Imm11 imm11 = Bits<0, 10>(instruction);
        v->thumb_BLX_suffix(/*X=*/true, imm11);
    })},
    { "BL/BLX (prefix)",         MakeMatcher("11110xxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        Imm11 imm11 = Bits<0, 10>(instruction);
        v->thumb_BLX_prefix(imm11);
    })},
    { "BL (suffix)",             MakeMatcher("11111xxxxxxxxxxx", [](Visitor* v, u16 instruction) {
        Imm11 imm11 = Bits<0, 10>(instruction);
        v->thumb_BLX_suffix(/*X=*/false, imm11);
    })}
}};

boost::optional<const ThumbInstruction&> DecodeThumb(u16 i) {
    // NOTE: The reverse search direction is important. Searching forwards would result in incorrect behavior.
    //       This is because the entries in thumb_instruction_table have more specific matches coming after less specific ones.
    //       Example:
    //           000ooxxxxxxxxxxx comes before 000110oxxxxxxxxx
    //       with a forward search direction notice how the first one will always be matched and the latter never will be.
    auto iterator = std::find_if(thumb_instruction_table.crbegin(), thumb_instruction_table.crend(),
        [i](const auto& instruction) { return instruction.Match(i); });

    return (iterator != thumb_instruction_table.crend()) ? boost::make_optional<const ThumbInstruction&>(*iterator) : boost::none;
}

};

#endif