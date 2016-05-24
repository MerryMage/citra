// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>

#include "common/assert.h"
#include "common/common_types.h"

#include "core/arm/jit/decoder/decoder.h"
#include "core/arm/jit/ir_builder.h"
#include "core/arm/jit/translate.h"
#include "core/memory.h"

namespace ArmJit {

using ArmReg = ArmDecoder::Register;
using ArmRegList = ArmDecoder::RegisterList;
using ArmImm4 = ArmDecoder::Imm4;
using ArmImm5 = ArmDecoder::Imm5;
using ArmImm8 = ArmDecoder::Imm8;
using ArmImm11 = ArmDecoder::Imm11;
using ArmImm12 = ArmDecoder::Imm12;
using ArmImm24 = ArmDecoder::Imm24;
using Cond = ArmDecoder::Cond;
using ShiftType = ArmDecoder::ShiftType;
using SignExtendRotation = ArmDecoder::SignExtendRotation;

class ArmTranslator final : private ArmDecoder::Visitor {
public:
    ArmTranslator(LocationDescriptor location) : ir(location), current(location) {}
    ~ArmTranslator() override {}

    MicroBlock Translate() {
        ASSERT(!stop_compilation);

        ir.block.location = current;

        do {
            TranslateSingleArmInstruction();
            instructions_translated++;
        } while (!stop_compilation && ((current.arm_pc & 0xFFF) != 0));

        stop_compilation = true;
        return ir.block;
    }

private:
    MicroBuilder ir;
    LocationDescriptor current;

    unsigned instructions_translated = 0;
    bool stop_compilation = false;

    void TranslateSingleArmInstruction() {
        u32 inst = Memory::Read32(current.arm_pc & 0xFFFFFFFC);

        auto inst_info = ArmDecoder::DecodeArm(inst);
        if (!inst_info) {
            // TODO: Log message
            FallbackToInterpreter();
        } else {
            inst_info->Visit(this, inst);
        }
    }

    std::array<std::shared_ptr<MicroValue>, 15> reg_values = {};
    std::shared_ptr<MicroValue> GetReg(ArmReg reg) {
        if (reg == ArmReg::PC)
            return ir.ConstU32(current.arm_pc + 8);

        size_t reg_index = static_cast<size_t>(reg);
        if (!reg_values[reg_index])
            reg_values[reg_index] = ir.GetGPR(reg);
        return reg_values[reg_index];
    }

    void FallbackToInterpreter() {
        ir.SetTerm(MicroBuilder::TermInterpret(current));
        stop_compilation = true;
    }

    // Branch instructions
    void B(Cond cond, ArmImm24 imm24) override;
    void BL(Cond cond, ArmImm24 imm24) override;
    void BLX_imm(bool H, ArmImm24 imm24) override;
    void BLX_reg(Cond cond, ArmReg Rm) override;
    void BX(Cond cond, ArmReg Rm) override;
    void BXJ(Cond cond, ArmReg Rm) override;

    // Coprocessor instructions
    void CDP() override;
    void LDC() override;
    void MCR() override;
    void MCRR() override;
    void MRC() override;
    void MRRC() override;
    void STC() override;

    // Data processing instructions
    void ADC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void ADC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void ADC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void ADD_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void ADD_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void ADD_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void AND_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void AND_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void AND_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void BIC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void BIC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void BIC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void CMN_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) override;
    void CMN_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void CMN_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void CMP_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) override;
    void CMP_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void CMP_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void EOR_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void EOR_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void EOR_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void MOV_imm(Cond cond, bool S,            ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void MOV_reg(Cond cond, bool S,            ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void MOV_rsr(Cond cond, bool S,            ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void MVN_imm(Cond cond, bool S,            ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void MVN_reg(Cond cond, bool S,            ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void MVN_rsr(Cond cond, bool S,            ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void ORR_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void ORR_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void ORR_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void RSB_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void RSB_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void RSB_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void RSC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void RSC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void RSC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void SBC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void SBC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void SBC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void SUB_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) override;
    void SUB_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void SUB_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void TEQ_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) override;
    void TEQ_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void TEQ_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) override;
    void TST_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) override;
    void TST_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void TST_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) override;

    // Exception generation instructions
    void BKPT(Cond cond, ArmImm12 imm12, ArmImm4 imm4) override;
    void SVC(Cond cond, ArmImm24 imm24) override;
    void UDF() override;

    // Extension functions
    void SXTAB(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void SXTAB16(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void SXTAH(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void SXTB(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void SXTB16(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void SXTH(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void UXTAB(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void UXTAB16(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void UXTAH(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void UXTB(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void UXTB16(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;
    void UXTH(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) override;

    // Hint instructions
    void PLD() override;
    void SEV() override;
    void WFE() override;
    void WFI() override;
    void YIELD() override;

    // Load/Store instructions
    void LDR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) override;
    void LDR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void LDRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) override;
    void LDRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void LDRBT() override;
    void LDRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void LDRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void LDRHT() override;
    void LDRSB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRSB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void LDRSBT() override;
    void LDRSH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRSH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void LDRSHT() override;
    void LDRT() override;
    void STR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) override;
    void STR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void STRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) override;
    void STRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) override;
    void STRBT() override;
    void STRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void STRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void STRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void STRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void STRHT() override;
    void STRT() override;

    // Load/Store multiple instructions
    void LDM(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmRegList list) override;
    void LDM_usr() override;
    void LDM_eret() override;
    void STM(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmRegList list) override;
    void STM_usr() override;

    // Miscellaneous instructions
    void CLZ(Cond cond, ArmReg Rd, ArmReg Rm) override;
    void NOP() override;
    void SEL(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;

    // Unsigned sum of absolute difference functions
    void USAD8(Cond cond, ArmReg Rd, ArmReg Rm, ArmReg Rn) override;
    void USADA8(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, ArmReg Rn) override;

    // Packing instructions
    void PKHBT(Cond cond, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ArmReg Rm) override;
    void PKHTB(Cond cond, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ArmReg Rm) override;

    // Reversal instructions
    void REV(Cond cond, ArmReg Rd, ArmReg Rm) override;
    void REV16(Cond cond, ArmReg Rd, ArmReg Rm) override;
    void REVSH(Cond cond, ArmReg Rd, ArmReg Rm) override;

    // Saturation instructions
    void SSAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd, ArmImm5 imm5, bool sh, ArmReg Rn) override;
    void SSAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd, ArmReg Rn) override;
    void USAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd, ArmImm5 imm5, bool sh, ArmReg Rn) override;
    void USAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd, ArmReg Rn) override;

    // Multiply (Normal) instructions
    void MLA(Cond cond, bool S, ArmReg Rd, ArmReg Ra, ArmReg Rm, ArmReg Rn) override;
    void MUL(Cond cond, bool S, ArmReg Rd, ArmReg Rm, ArmReg Rn) override;

    // Multiply (Long) instructions
    void SMLAL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) override;
    void SMULL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) override;
    void UMAAL(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) override;
    void UMLAL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) override;
    void UMULL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) override;

    // Multiply (Halfword) instructions
    void SMLALxy(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, bool M, bool N, ArmReg Rn) override;
    void SMLAxy(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, bool N, ArmReg Rn) override;
    void SMULxy(Cond cond, ArmReg Rd, ArmReg Rm, bool M, bool N, ArmReg Rn) override;

    // Multiply (word by halfword) instructions
    void SMLAWy(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, ArmReg Rn) override;
    void SMULWy(Cond cond, ArmReg Rd, ArmReg Rm, bool M, ArmReg Rn) override;

    // Multiply (Most significant word) instructions
    void SMMLA(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool R, ArmReg Rn) override;
    void SMMLS(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool R, ArmReg Rn) override;
    void SMMUL(Cond cond, ArmReg Rd, ArmReg Rm, bool R, ArmReg Rn) override;

    // Multiply (Dual) instructions
    void SMLAD(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, ArmReg Rn) override;
    void SMLALD(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, bool M, ArmReg Rn) override;
    void SMLSD(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, ArmReg Rn) override;
    void SMLSLD(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, bool M, ArmReg Rn) override;
    void SMUAD(Cond cond, ArmReg Rd, ArmReg Rm, bool M, ArmReg Rn) override;
    void SMUSD(Cond cond, ArmReg Rd, ArmReg Rm, bool M, ArmReg Rn) override;

    // Parallel Add/Subtract (Modulo arithmetic) instructions
    void SADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void USAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void USUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void USUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;

    // Parallel Add/Subtract (Saturating) instructions
    void QADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UQADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UQADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UQASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UQSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UQSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UQSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;

    // Parallel Add/Subtract (Halving) instructions
    void SHADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SHADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SHASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SHSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SHSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SHSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UHADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UHADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UHASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UHSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UHSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void UHSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;

    // Saturated Add/Subtract instructions
    void QADD(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QSUB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QDADD(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void QDSUB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;

    // Synchronization Primitive instructions
    void CLREX() override;
    void LDREX(Cond cond, ArmReg Rn, ArmReg Rd) override;
    void LDREXB(Cond cond, ArmReg Rn, ArmReg Rd) override;
    void LDREXD(Cond cond, ArmReg Rn, ArmReg Rd) override;
    void LDREXH(Cond cond, ArmReg Rn, ArmReg Rd) override;
    void STREX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void STREXB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void STREXD(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void STREXH(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SWP(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;
    void SWPB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) override;

    // Status register access instructions
    void CPS() override;
    void MRS() override;
    void MSR() override;
    void RFE() override;
    void SETEND(bool E) override;
    void SRS() override;

    // Thumb specific instructions
    void thumb_B(Cond cond, ArmImm8 imm8) override;
    void thumb_B(ArmImm11 imm11) override;
    void thumb_BLX_prefix(ArmImm11 imm11) override;
    void thumb_BLX_suffix(bool L, ArmImm11 imm11) override;
};

MicroBlock Translate(const LocationDescriptor& location) {
    ArmTranslator translator(location);
    return translator.Translate();
}

// Branch instructions
void ArmTranslator::B(Cond cond, ArmImm24 imm24) { FallbackToInterpreter(); }
void ArmTranslator::BL(Cond cond, ArmImm24 imm24) { FallbackToInterpreter(); }
void ArmTranslator::BLX_imm(bool H, ArmImm24 imm24) { FallbackToInterpreter(); }
void ArmTranslator::BLX_reg(Cond cond, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::BX(Cond cond, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::BXJ(Cond cond, ArmReg Rm) { FallbackToInterpreter(); }

// Coprocessor instructions
void ArmTranslator::CDP() { FallbackToInterpreter(); }
void ArmTranslator::LDC() { FallbackToInterpreter(); }
void ArmTranslator::MCR() { FallbackToInterpreter(); }
void ArmTranslator::MCRR() { FallbackToInterpreter(); }
void ArmTranslator::MRC() { FallbackToInterpreter(); }
void ArmTranslator::MRRC() { FallbackToInterpreter(); }
void ArmTranslator::STC() { FallbackToInterpreter(); }

// Data processing instructions
void ArmTranslator::ADC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::ADC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::ADC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::ADD_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::ADD_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::ADD_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::AND_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::AND_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::AND_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::BIC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::BIC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::BIC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::CMN_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::CMN_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::CMN_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::CMP_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::CMP_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::CMP_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::EOR_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::EOR_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::EOR_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::MOV_imm(Cond cond, bool S,            ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::MOV_reg(Cond cond, bool S,            ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::MOV_rsr(Cond cond, bool S,            ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::MVN_imm(Cond cond, bool S,            ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::MVN_reg(Cond cond, bool S,            ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::MVN_rsr(Cond cond, bool S,            ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::ORR_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::ORR_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::ORR_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::RSB_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::RSB_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::RSB_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::RSC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::RSC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::RSC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SBC_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::SBC_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SBC_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SUB_imm(Cond cond, bool S, ArmReg Rn, ArmReg Rd, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::SUB_reg(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SUB_rsr(Cond cond, bool S, ArmReg Rn, ArmReg Rd, ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::TEQ_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::TEQ_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::TEQ_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::TST_imm(Cond cond,         ArmReg Rn,            int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::TST_reg(Cond cond,         ArmReg Rn,            ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::TST_rsr(Cond cond,         ArmReg Rn,            ArmReg Rs, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }

// Exception generation instructions
void ArmTranslator::BKPT(Cond cond, ArmImm12 imm12, ArmImm4 imm4) { FallbackToInterpreter(); }
void ArmTranslator::SVC(Cond cond, ArmImm24 imm24) { FallbackToInterpreter(); }
void ArmTranslator::UDF() { FallbackToInterpreter(); }

// Extension functions
void ArmTranslator::SXTAB(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SXTAB16(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SXTAH(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SXTB(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SXTB16(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SXTH(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UXTAB(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UXTAB16(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UXTAH(Cond cond, ArmReg Rn, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UXTB(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UXTB16(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UXTH(Cond cond, ArmReg Rd, SignExtendRotation rotate, ArmReg Rm) { FallbackToInterpreter(); }

// Hint instructions
void ArmTranslator::PLD() { FallbackToInterpreter(); }
void ArmTranslator::SEV() { FallbackToInterpreter(); }
void ArmTranslator::WFE() { FallbackToInterpreter(); }
void ArmTranslator::WFI() { FallbackToInterpreter(); }
void ArmTranslator::YIELD() { FallbackToInterpreter(); }

// Load/Store instructions
void ArmTranslator::LDR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::LDR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::LDRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::LDRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::LDRBT() { FallbackToInterpreter(); }
void ArmTranslator::LDRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::LDRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::LDRHT() { FallbackToInterpreter(); }
void ArmTranslator::LDRSB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRSB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::LDRSBT() { FallbackToInterpreter(); }
void ArmTranslator::LDRSH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRSH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::LDRSHT() { FallbackToInterpreter(); }
void ArmTranslator::LDRT() { FallbackToInterpreter(); }
void ArmTranslator::STR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::STR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::STRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::STRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ShiftType shift, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::STRBT() { FallbackToInterpreter(); }
void ArmTranslator::STRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::STRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::STRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::STRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::STRHT() { FallbackToInterpreter(); }
void ArmTranslator::STRT() { FallbackToInterpreter(); }

// Load/Store multiple instructions
void ArmTranslator::LDM(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmRegList list) { FallbackToInterpreter(); }
void ArmTranslator::LDM_usr() { FallbackToInterpreter(); }
void ArmTranslator::LDM_eret() { FallbackToInterpreter(); }
void ArmTranslator::STM(Cond cond, bool P, bool U, bool W, ArmReg Rn, ArmRegList list) { FallbackToInterpreter(); }
void ArmTranslator::STM_usr() { FallbackToInterpreter(); }

// Miscellaneous instructions
void ArmTranslator::CLZ(Cond cond, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::NOP() { FallbackToInterpreter(); }
void ArmTranslator::SEL(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }

// Unsigned sum of absolute difference functions
void ArmTranslator::USAD8(Cond cond, ArmReg Rd, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::USADA8(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }

// Packing instructions
void ArmTranslator::PKHBT(Cond cond, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::PKHTB(Cond cond, ArmReg Rn, ArmReg Rd, ArmImm5 imm5, ArmReg Rm) { FallbackToInterpreter(); }

// Reversal instructions
void ArmTranslator::REV(Cond cond, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::REV16(Cond cond, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::REVSH(Cond cond, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }

// Saturation instructions
void ArmTranslator::SSAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd, ArmImm5 imm5, bool sh, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SSAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::USAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd, ArmImm5 imm5, bool sh, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::USAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd, ArmReg Rn) { FallbackToInterpreter(); }

// Multiply (Normal) instructions
void ArmTranslator::MLA(Cond cond, bool S, ArmReg Rd, ArmReg Ra, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::MUL(Cond cond, bool S, ArmReg Rd, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }

// Multiply (Long) instructions
void ArmTranslator::SMLAL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMULL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::UMAAL(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::UMLAL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::UMULL(Cond cond, bool S, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, ArmReg Rn) { FallbackToInterpreter(); }

// Multiply (Halfword) instructions
void ArmTranslator::SMLALxy(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, bool M, bool N, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMLAxy(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, bool N, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMULxy(Cond cond, ArmReg Rd, ArmReg Rm, bool M, bool N, ArmReg Rn) { FallbackToInterpreter(); }

// Multiply (word by halfword) instructions
void ArmTranslator::SMLAWy(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMULWy(Cond cond, ArmReg Rd, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }

// Multiply (Most significant word) instructions
void ArmTranslator::SMMLA(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool R, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMMLS(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool R, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMMUL(Cond cond, ArmReg Rd, ArmReg Rm, bool R, ArmReg Rn) { FallbackToInterpreter(); }

// Multiply (Dual) instructions
void ArmTranslator::SMLAD(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMLALD(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMLSD(Cond cond, ArmReg Rd, ArmReg Ra, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMLSLD(Cond cond, ArmReg RdHi, ArmReg RdLo, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMUAD(Cond cond, ArmReg Rd, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }
void ArmTranslator::SMUSD(Cond cond, ArmReg Rd, ArmReg Rm, bool M, ArmReg Rn) { FallbackToInterpreter(); }

// Parallel Add/Subtract (Modulo arithmetic) instructions
void ArmTranslator::SADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::USAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::USUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::USUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }

// Parallel Add/Subtract (Saturating) instructions
void ArmTranslator::QADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UQADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UQADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UQASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UQSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UQSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UQSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }

// Parallel Add/Subtract (Halving) instructions
void ArmTranslator::SHADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SHADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SHASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SHSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SHSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SHSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UHADD8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UHADD16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UHASX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UHSAX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UHSUB8(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::UHSUB16(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }

// Saturated Add/Subtract instructions
void ArmTranslator::QADD(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QSUB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QDADD(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::QDSUB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }

// Synchronization Primitive instructions
void ArmTranslator::CLREX() { FallbackToInterpreter(); }
void ArmTranslator::LDREX(Cond cond, ArmReg Rn, ArmReg Rd) { FallbackToInterpreter(); }
void ArmTranslator::LDREXB(Cond cond, ArmReg Rn, ArmReg Rd) { FallbackToInterpreter(); }
void ArmTranslator::LDREXD(Cond cond, ArmReg Rn, ArmReg Rd) { FallbackToInterpreter(); }
void ArmTranslator::LDREXH(Cond cond, ArmReg Rn, ArmReg Rd) { FallbackToInterpreter(); }
void ArmTranslator::STREX(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::STREXB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::STREXD(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::STREXH(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SWP(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }
void ArmTranslator::SWPB(Cond cond, ArmReg Rn, ArmReg Rd, ArmReg Rm) { FallbackToInterpreter(); }

// Status register access instructions
void ArmTranslator::CPS() { FallbackToInterpreter(); }
void ArmTranslator::MRS() { FallbackToInterpreter(); }
void ArmTranslator::MSR() { FallbackToInterpreter(); }
void ArmTranslator::RFE() { FallbackToInterpreter(); }
void ArmTranslator::SETEND(bool E) { FallbackToInterpreter(); }
void ArmTranslator::SRS() { FallbackToInterpreter(); }

// Thumb specific instructions
void ArmTranslator::thumb_B(Cond cond, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::thumb_B(ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::thumb_BLX_prefix(ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::thumb_BLX_suffix(bool L, ArmImm11 imm11) { FallbackToInterpreter(); }

} // namespace Jit
