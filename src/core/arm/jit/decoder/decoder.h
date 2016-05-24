// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>

#include <boost/optional.hpp>

#include "common/common_types.h"

#include "core/arm/jit/common.h"

namespace ArmDecoder {

// This is a generic ARMv6K decoder using double dispatch.

class ArmInstruction;
class ThumbInstruction;
class Visitor;

/**
 * This function identifies an ARM instruction and returns the relevant ArmInstruction.
 * Returns boost::none if the instruction was not recognised.
 */
boost::optional<const ArmInstruction&> DecodeArm(u32 instruction);

/**
* This function identifies a Thumb instruction and returns the relevant ThumbInstruction.
* Returns boost::none if the instruction was not recognised.
*/
boost::optional<const ThumbInstruction&> DecodeThumb(u16 instruction);

/// INTERNAL
struct ArmMatcher {
    virtual ~ArmMatcher() = default;
    u32 bit_mask;
    u32 expected;
    bool Match(u32 x) const {
        return (x & bit_mask) == expected;
    }
    virtual void visit(Visitor* v, u32 inst) = 0;
};

/**
 * This structure represents a decoder for a specific ARM instruction.
 * Calling Visit calls the relevant function on Visitor.
 */
class ArmInstruction final {
public:
    ArmInstruction(const char* const name, std::unique_ptr<ArmMatcher> matcher) : name(name), matcher(std::move(matcher)) {}

    const char* Name() const {
        return name;
    }

    bool Match(u32 instruction) const {
        return matcher->Match(instruction);
    }

    void Visit(Visitor* v, u32 instruction) const {
        matcher->visit(v, instruction);
    }

private:
    const char* const name;
    const std::unique_ptr<ArmMatcher> matcher;
};

/// INTERNAL
struct ThumbMatcher {
    u16 bit_mask;
    u16 expected;
    bool Match(u16 x) const {
        return (x & bit_mask) == expected;
    }
    std::function<void(Visitor*, u16 inst)> visit;
};

/**
 * This structure represents a decoder for a specific Thumb instruction.
 * Calling Visit calls the relevant function on Visitor.
 */
class ThumbInstruction final {
public:
    ThumbInstruction(const char* const name, ThumbMatcher&& matcher) : name(name), matcher(std::move(matcher)) {}

    const char* Name() const {
        return name;
    }

    bool Match(u16 instruction) const {
        return matcher.Match(instruction);
    }

    void Visit(Visitor* v, u16 instruction) const {
        matcher.visit(v, instruction);
    }

private:
    const char* const name;
    const ThumbMatcher matcher;
};

using Cond = ArmJit::Cond;

using Imm4 = u32;
using Imm5 = u32;
using Imm8 = u32;
using Imm11 = u32;
using Imm12 = u32;
using Imm24 = u32;
using RegisterList = u16;

using Register = ArmJit::ArmReg;

enum class ShiftType {
    LSL,
    LSR,
    ASR,
    ROR ///< RRX falls under this too
};

enum class SignExtendRotation {
    ROR_0,  ///< ROR #0 or omitted
    ROR_8,  ///< ROR #8
    ROR_16, ///< ROR #16
    ROR_24  ///< ROR #24
};

class Visitor {
public:
    virtual ~Visitor() = default;

    // Branch instructions
    virtual void B(Cond cond, Imm24 imm24) = 0;
    virtual void BL(Cond cond, Imm24 imm24) = 0;
    virtual void BLX_imm(bool H, Imm24 imm24) = 0;
    virtual void BLX_reg(Cond cond, Register Rm) = 0;
    virtual void BX(Cond cond, Register Rm) = 0;
    virtual void BXJ(Cond cond, Register Rm) = 0;

    // Coprocessor instructions
    virtual void CDP() = 0;
    virtual void LDC() = 0;
    virtual void MCR() = 0;
    virtual void MCRR() = 0;
    virtual void MRC() = 0;
    virtual void MRRC() = 0;
    virtual void STC() = 0;

    // Data processing instructions
    virtual void ADC_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void ADC_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void ADC_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void ADD_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void ADD_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void ADD_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void AND_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void AND_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void AND_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void BIC_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void BIC_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void BIC_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void CMN_imm(Cond cond, Register Rn, int rotate, Imm8 imm8) = 0;
    virtual void CMN_reg(Cond cond, Register Rn, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void CMN_rsr(Cond cond, Register Rn, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void CMP_imm(Cond cond, Register Rn, int rotate, Imm8 imm8) = 0;
    virtual void CMP_reg(Cond cond, Register Rn, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void CMP_rsr(Cond cond, Register Rn, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void EOR_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void EOR_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void EOR_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void MOV_imm(Cond cond, bool S, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void MOV_reg(Cond cond, bool S, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void MOV_rsr(Cond cond, bool S, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void MVN_imm(Cond cond, bool S, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void MVN_reg(Cond cond, bool S, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void MVN_rsr(Cond cond, bool S, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void ORR_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void ORR_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void ORR_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void RSB_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void RSB_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void RSB_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void RSC_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void RSC_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void RSC_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void SBC_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void SBC_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void SBC_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void SUB_imm(Cond cond, bool S, Register Rn, Register Rd, int rotate, Imm8 imm8) = 0;
    virtual void SUB_reg(Cond cond, bool S, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void SUB_rsr(Cond cond, bool S, Register Rn, Register Rd, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void TEQ_imm(Cond cond, Register Rn, int rotate, Imm8 imm8) = 0;
    virtual void TEQ_reg(Cond cond, Register Rn, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void TEQ_rsr(Cond cond, Register Rn, Register Rs, ShiftType shift, Register Rm) = 0;
    virtual void TST_imm(Cond cond, Register Rn, int rotate, Imm8 imm8) = 0;
    virtual void TST_reg(Cond cond, Register Rn, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void TST_rsr(Cond cond, Register Rn, Register Rs, ShiftType shift, Register Rm) = 0;

    // Exception generation instructions
    virtual void BKPT(Cond cond, Imm12 imm12, Imm4 imm4) = 0;
    virtual void SVC(Cond cond, Imm24 imm24) = 0;
    virtual void UDF() = 0;

    // Extension functions
    virtual void SXTAB(Cond cond, Register Rn, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void SXTAB16(Cond cond, Register Rn, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void SXTAH(Cond cond, Register Rn, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void SXTB(Cond cond, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void SXTB16(Cond cond, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void SXTH(Cond cond, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void UXTAB(Cond cond, Register Rn, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void UXTAB16(Cond cond, Register Rn, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void UXTAH(Cond cond, Register Rn, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void UXTB(Cond cond, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void UXTB16(Cond cond, Register Rd, SignExtendRotation rotate, Register Rm) = 0;
    virtual void UXTH(Cond cond, Register Rd, SignExtendRotation rotate, Register Rm) = 0;

    // Hint instructions
    virtual void PLD() = 0;
    virtual void SEV() = 0;
    virtual void WFE() = 0;
    virtual void WFI() = 0;
    virtual void YIELD() = 0;

    // Load/Store instructions
    virtual void LDR_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm12 imm12) = 0;
    virtual void LDR_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void LDRB_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm12 imm12) = 0;
    virtual void LDRB_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void LDRBT() = 0;
    virtual void LDRD_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm4 imm8a, Imm4 imm8b) = 0;
    virtual void LDRD_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Register Rm) = 0;
    virtual void LDRH_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm4 imm8a, Imm4 imm8b) = 0;
    virtual void LDRH_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Register Rm) = 0;
    virtual void LDRHT() = 0;
    virtual void LDRSB_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm4 imm8a, Imm4 imm8b) = 0;
    virtual void LDRSB_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Register Rm) = 0;
    virtual void LDRSBT() = 0;
    virtual void LDRSH_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm4 imm8a, Imm4 imm8b) = 0;
    virtual void LDRSH_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Register Rm) = 0;
    virtual void LDRSHT() = 0;
    virtual void LDRT() = 0;
    virtual void STR_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm12 imm12) = 0;
    virtual void STR_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void STRB_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm12 imm12) = 0;
    virtual void STRB_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm5 imm5, ShiftType shift, Register Rm) = 0;
    virtual void STRBT() = 0;
    virtual void STRD_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm4 imm8a, Imm4 imm8b) = 0;
    virtual void STRD_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Register Rm) = 0;
    virtual void STRH_imm(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Imm4 imm8a, Imm4 imm8b) = 0;
    virtual void STRH_reg(Cond cond, bool P, bool U, bool W, Register Rn, Register Rd, Register Rm) = 0;
    virtual void STRHT() = 0;
    virtual void STRT() = 0;

    // Load/Store multiple instructions
    virtual void LDM(Cond cond, bool P, bool U, bool W, Register Rn, RegisterList list) = 0;
    virtual void LDM_usr() = 0;
    virtual void LDM_eret() = 0;
    virtual void STM(Cond cond, bool P, bool U, bool W, Register Rn, RegisterList list) = 0;
    virtual void STM_usr() = 0;

    // Miscellaneous instructions
    virtual void CLZ(Cond cond, Register Rd, Register Rm) = 0;
    virtual void NOP() = 0;
    virtual void SEL(Cond cond, Register Rn, Register Rd, Register Rm) = 0;

    // Unsigned sum of absolute difference functions
    virtual void USAD8(Cond cond, Register Rd, Register Rm, Register Rn) = 0;
    virtual void USADA8(Cond cond, Register Rd, Register Ra, Register Rm, Register Rn) = 0;

    // Packing instructions
    virtual void PKHBT(Cond cond, Register Rn, Register Rd, Imm5 imm5, Register Rm) = 0;
    virtual void PKHTB(Cond cond, Register Rn, Register Rd, Imm5 imm5, Register Rm) = 0;

    // Reversal instructions
    virtual void REV(Cond cond, Register Rd, Register Rm) = 0;
    virtual void REV16(Cond cond, Register Rd, Register Rm) = 0;
    virtual void REVSH(Cond cond, Register Rd, Register Rm) = 0;

    // Saturation instructions
    virtual void SSAT(Cond cond, Imm5 sat_imm, Register Rd, Imm5 imm5, bool sh, Register Rn) = 0;
    virtual void SSAT16(Cond cond, Imm4 sat_imm, Register Rd, Register Rn) = 0;
    virtual void USAT(Cond cond, Imm5 sat_imm, Register Rd, Imm5 imm5, bool sh, Register Rn) = 0;
    virtual void USAT16(Cond cond, Imm4 sat_imm, Register Rd, Register Rn) = 0;

    // Multiply (Normal) instructions
    virtual void MLA(Cond cond, bool S, Register Rd, Register Ra, Register Rm, Register Rn) = 0;
    virtual void MUL(Cond cond, bool S, Register Rd, Register Rm, Register Rn) = 0;

    // Multiply (Long) instructions
    virtual void SMLAL(Cond cond, bool S, Register RdHi, Register RdLo, Register Rm, Register Rn) = 0;
    virtual void SMULL(Cond cond, bool S, Register RdHi, Register RdLo, Register Rm, Register Rn) = 0;
    virtual void UMAAL(Cond cond, Register RdHi, Register RdLo, Register Rm, Register Rn) = 0;
    virtual void UMLAL(Cond cond, bool S, Register RdHi, Register RdLo, Register Rm, Register Rn) = 0;
    virtual void UMULL(Cond cond, bool S, Register RdHi, Register RdLo, Register Rm, Register Rn) = 0;

    // Multiply (Halfword) instructions
    virtual void SMLALxy(Cond cond, Register RdHi, Register RdLo, Register Rm, bool M, bool N, Register Rn) = 0;
    virtual void SMLAxy(Cond cond, Register Rd, Register Ra, Register Rm, bool M, bool N, Register Rn) = 0;
    virtual void SMULxy(Cond cond, Register Rd, Register Rm, bool M, bool N, Register Rn) = 0;

    // Multiply (word by halfword) instructions
    virtual void SMLAWy(Cond cond, Register Rd, Register Ra, Register Rm, bool M, Register Rn) = 0;
    virtual void SMULWy(Cond cond, Register Rd, Register Rm, bool M, Register Rn) = 0;

    // Multiply (Most significant word) instructions
    virtual void SMMLA(Cond cond, Register Rd, Register Ra, Register Rm, bool R, Register Rn) = 0;
    virtual void SMMLS(Cond cond, Register Rd, Register Ra, Register Rm, bool R, Register Rn) = 0;
    virtual void SMMUL(Cond cond, Register Rd, Register Rm, bool R, Register Rn) = 0;

    // Multiply (Dual) instructions
    virtual void SMLAD(Cond cond, Register Rd, Register Ra, Register Rm, bool M, Register Rn) = 0;
    virtual void SMLALD(Cond cond, Register RdHi, Register RdLo, Register Rm, bool M, Register Rn) = 0;
    virtual void SMLSD(Cond cond, Register Rd, Register Ra, Register Rm, bool M, Register Rn) = 0;
    virtual void SMLSLD(Cond cond, Register RdHi, Register RdLo, Register Rm, bool M, Register Rn) = 0;
    virtual void SMUAD(Cond cond, Register Rd, Register Rm, bool M, Register Rn) = 0;
    virtual void SMUSD(Cond cond, Register Rd, Register Rm, bool M, Register Rn) = 0;

    // Parallel Add/Subtract (Modulo arithmetic) instructions
    virtual void SADD8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SADD16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SASX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SSAX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SSUB8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SSUB16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UADD8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UADD16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UASX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void USAX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void USUB8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void USUB16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;

    // Parallel Add/Subtract (Saturating) instructions
    virtual void QADD8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QADD16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QASX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QSAX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QSUB8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QSUB16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UQADD8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UQADD16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UQASX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UQSAX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UQSUB8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UQSUB16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;

    // Parallel Add/Subtract (Halving) instructions
    virtual void SHADD8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SHADD16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SHASX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SHSAX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SHSUB8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SHSUB16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UHADD8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UHADD16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UHASX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UHSAX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UHSUB8(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void UHSUB16(Cond cond, Register Rn, Register Rd, Register Rm) = 0;

    // Saturated Add/Subtract instructions
    virtual void QADD(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QSUB(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QDADD(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void QDSUB(Cond cond, Register Rn, Register Rd, Register Rm) = 0;

    // Synchronization Primitive instructions
    virtual void CLREX() = 0;
    virtual void LDREX(Cond cond, Register Rn, Register Rd) = 0;
    virtual void LDREXB(Cond cond, Register Rn, Register Rd) = 0;
    virtual void LDREXD(Cond cond, Register Rn, Register Rd) = 0;
    virtual void LDREXH(Cond cond, Register Rn, Register Rd) = 0;
    virtual void STREX(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void STREXB(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void STREXD(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void STREXH(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SWP(Cond cond, Register Rn, Register Rd, Register Rm) = 0;
    virtual void SWPB(Cond cond, Register Rn, Register Rd, Register Rm) = 0;

    // Status register access instructions
    virtual void CPS() = 0;
    virtual void MRS() = 0;
    virtual void MSR() = 0;
    virtual void RFE() = 0;
    virtual void SETEND(bool E) = 0;
    virtual void SRS() = 0;

    // Thumb specific instructions
    virtual void thumb_B(Cond cond, Imm8 imm8) = 0;
    virtual void thumb_B(Imm11 imm11) = 0;
    virtual void thumb_BLX_prefix(Imm11 imm11) = 0;
    virtual void thumb_BLX_suffix(bool X, Imm11 imm11) = 0;
};

} // namespace ArmDecoder