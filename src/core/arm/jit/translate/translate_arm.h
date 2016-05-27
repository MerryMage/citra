// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>

#include "common/common_types.h"

#include "core/arm/jit/decoder/decoder.h"
#include "core/arm/jit/ir/micro_builder.h"

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
    ArmTranslator(LocationDescriptor location);
    ~ArmTranslator() override {}

    MicroBlock Translate();

private:
    // State
    MicroBuilder ir;
    LocationDescriptor current;
    size_t instructions_translated = 0;
    bool stop_compilation = false;

    // GetReg/SetReg
    std::array<std::shared_ptr<MicroValue>, 15> reg_values = {};
    std::shared_ptr<MicroValue> GetReg(ArmReg reg);
    void SetReg(ArmReg reg, std::shared_ptr<MicroValue> value);

    // Translate instruction
    void TranslateSingleArmInstruction();

    // Helper functions
    void FallbackToInterpreter();
    bool ConditionPassed(Cond cond);
    u32 ArmExpandImm(u32 imm8, int rotate);
    u32 PC() const;
    void ALUWritePC(std::shared_ptr<MicroValue> new_pc);
    void LoadWritePC(std::shared_ptr<MicroValue> new_pc);
    void BranchWritePC(u32 new_pc);
    void BranchWritePC(std::shared_ptr<MicroValue> new_pc);
    void BXWritePC(u32 new_pc);
    void BXWritePC(std::shared_ptr<MicroValue> new_pc);

    // Branch instructions
    void B(Cond cond, ArmImm24 imm24) override;
    void BL(Cond cond, ArmImm24 imm24) override;
    void BLX_imm(bool H, ArmImm24 imm24) override;
    void BLX_reg(Cond cond, ArmReg Rm_index) override;
    void BX(Cond cond, ArmReg Rm_index) override;
    void BXJ(Cond cond, ArmReg Rm_index) override;

    // Coprocessor instructions
    void CDP() override;
    void LDC() override;
    void MCR() override;
    void MCRR() override;
    void MRC() override;
    void MRRC() override;
    void STC() override;

    // Data processing instructions
    void ADC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void ADC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void ADC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void ADD_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void ADD_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void ADD_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void AND_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void AND_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void AND_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void BIC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void BIC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void BIC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void CMN_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) override;
    void CMN_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void CMN_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void CMP_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) override;
    void CMP_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void CMP_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void EOR_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void EOR_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void EOR_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void MOV_imm(Cond cond, bool S, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void MOV_reg(Cond cond, bool S, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void MOV_rsr(Cond cond, bool S, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void MVN_imm(Cond cond, bool S, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void MVN_reg(Cond cond, bool S, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void MVN_rsr(Cond cond, bool S, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void ORR_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void ORR_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void ORR_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void RSB_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void RSB_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void RSB_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void RSC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void RSC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void RSC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void SBC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void SBC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void SBC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void SUB_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) override;
    void SUB_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void SUB_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void TEQ_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) override;
    void TEQ_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void TEQ_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;
    void TST_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) override;
    void TST_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void TST_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) override;

    // Exception generation instructions
    void BKPT(Cond cond, ArmImm12 imm12, ArmImm4 imm4) override;
    void SVC(Cond cond, ArmImm24 imm24) override;
    void UDF() override;

    // Extension functions
    void SXTAB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void SXTAB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void SXTAH(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void SXTB(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void SXTB16(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void SXTH(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void UXTAB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void UXTAB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void UXTAH(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void UXTB(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void UXTB16(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;
    void UXTH(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) override;

    // Hint instructions
    void PLD() override;
    void SEV() override;
    void WFE() override;
    void WFI() override;
    void YIELD() override;

    // Load/Store instructions
    void LDR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) override;
    void LDR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void LDRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) override;
    void LDRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void LDRBT() override;
    void LDRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void LDRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void LDRHT() override;
    void LDRSB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRSB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void LDRSBT() override;
    void LDRSH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void LDRSH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void LDRSHT() override;
    void LDRT() override;
    void STR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) override;
    void STR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void STRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) override;
    void STRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) override;
    void STRBT() override;
    void STRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void STRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void STRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) override;
    void STRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void STRHT() override;
    void STRT() override;

    // Load/Store multiple instructions
    void LDM(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmRegList list) override;
    void LDM_usr() override;
    void LDM_eret() override;
    void STM(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmRegList list) override;
    void STM_usr() override;

    // Miscellaneous instructions
    void CLZ(Cond cond, ArmReg Rd_index, ArmReg Rm_index) override;
    void NOP() override;
    void SEL(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;

    // Unsigned sum of absolute difference functions
    void USAD8(Cond cond, ArmReg Rd_index, ArmReg Rm_index, ArmReg Rn_index) override;
    void USADA8(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, ArmReg Rn_index) override;

    // Packing instructions
    void PKHBT(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ArmReg Rm_index) override;
    void PKHTB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ArmReg Rm_index) override;

    // Reversal instructions
    void REV(Cond cond, ArmReg Rd_index, ArmReg Rm_index) override;
    void REV16(Cond cond, ArmReg Rd_index, ArmReg Rm_index) override;
    void REVSH(Cond cond, ArmReg Rd_index, ArmReg Rm_index) override;

    // Saturation instructions
    void SSAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd_index, ArmImm5 imm5, bool sh, ArmReg Rn_index) override;
    void SSAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd_index, ArmReg Rn_index) override;
    void USAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd_index, ArmImm5 imm5, bool sh, ArmReg Rn_index) override;
    void USAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd_index, ArmReg Rn_index) override;

    // Multiply (Normal) instructions
    void MLA(Cond cond, bool S, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, ArmReg Rn_index) override;
    void MUL(Cond cond, bool S, ArmReg Rd_index, ArmReg Rm_index, ArmReg Rn_index) override;

    // Multiply (Long) instructions
    void SMLAL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) override;
    void SMULL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) override;
    void UMAAL(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) override;
    void UMLAL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) override;
    void UMULL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) override;

    // Multiply (Halfword) instructions
    void SMLALxy(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, bool M, bool N, ArmReg Rn_index) override;
    void SMLAxy(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, bool N, ArmReg Rn_index) override;
    void SMULxy(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, bool N, ArmReg Rn_index) override;

    // Multiply (word by halfword) instructions
    void SMLAWy(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, ArmReg Rn_index) override;
    void SMULWy(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, ArmReg Rn_index) override;

    // Multiply (Most significant word) instructions
    void SMMLA(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool R, ArmReg Rn_index) override;
    void SMMLS(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool R, ArmReg Rn_index) override;
    void SMMUL(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool R, ArmReg Rn_index) override;

    // Multiply (Dual) instructions
    void SMLAD(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, ArmReg Rn_index) override;
    void SMLALD(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, bool M, ArmReg Rn_index) override;
    void SMLSD(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, ArmReg Rn_index) override;
    void SMLSLD(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, bool M, ArmReg Rn_index) override;
    void SMUAD(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, ArmReg Rn_index) override;
    void SMUSD(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, ArmReg Rn_index) override;

    // Parallel Add/Subtract (Modulo arithmetic) instructions
    void SADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void USAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void USUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void USUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;

    // Parallel Add/Subtract (Saturating) instructions
    void QADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UQADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UQADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UQASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UQSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UQSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UQSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;

    // Parallel Add/Subtract (Halving) instructions
    void SHADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SHADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SHASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SHSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SHSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SHSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UHADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UHADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UHASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UHSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UHSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void UHSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;

    // Saturated Add/Subtract instructions
    void QADD(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QSUB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QDADD(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void QDSUB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;

    // Synchronization Primitive instructions
    void CLREX() override;
    void LDREX(Cond cond, ArmReg Rn_index, ArmReg Rd_index) override;
    void LDREXB(Cond cond, ArmReg Rn_index, ArmReg Rd_index) override;
    void LDREXD(Cond cond, ArmReg Rn_index, ArmReg Rd_index) override;
    void LDREXH(Cond cond, ArmReg Rn_index, ArmReg Rd_index) override;
    void STREX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void STREXB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void STREXD(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void STREXH(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SWP(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;
    void SWPB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) override;

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

} // namespace Jit
