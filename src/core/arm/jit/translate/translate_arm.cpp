// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/arm/jit/ir/micro_ir.h"
#include "core/arm/jit/jit_common.h"
#include "core/arm/jit/translate/translate_arm.h"
#include "core/memory.h"
#include <common/bit_util.h>

namespace ArmJit {

ArmTranslator::ArmTranslator(LocationDescriptor location): ir(location), current(location) {}

MicroBlock ArmTranslator::Translate() {
    ASSERT(!stop_compilation);

    ir.block.location = current;
    do {
        instructions_translated++;
        TranslateSingleArmInstruction();
    } while (!stop_compilation && (current.arm_pc & 0xFFF) != 0);

    if (!stop_compilation) {
        // We terminated translation purely because we hit a page boundary.
        ir.SetTerm(MicroTerm::LinkBlock(current));
    }

    // We've taken the values out of the GPRs and played around with them for a bit, it's time to put them back.
    for (size_t i = 0; i < reg_values.size(); i++) {
        if (reg_values[i] && reg_values[i]->GetOp() != MicroOp::GetGPR) {
            ir.SetGPR(static_cast<ArmReg>(i), reg_values[i]);
        }
    }

    ir.block.cycles_consumed = instructions_translated;

    stop_compilation = true;
    return ir.block;
}

void ArmTranslator::TranslateSingleArmInstruction() {
    u32 inst = Memory::Read32(current.arm_pc & 0xFFFFFFFC);

    auto inst_info = ArmDecoder::DecodeArm(inst);
    if (!inst_info) {
        // TODO: Log message
        FallbackToInterpreter();
    } else {
        const auto old = current;
        inst_info->Visit(this, inst);
        ASSERT(old == current); // Instruction translators should not modify the LocationDescriptor.
        current.arm_pc += 4;
    }
}

std::shared_ptr<MicroValue> ArmTranslator::GetReg(ArmReg reg) {
    if (reg == ArmReg::PC)
        return ir.ConstU32(current.arm_pc + 8);

    size_t reg_index = static_cast<size_t>(reg);
    if (!reg_values[reg_index])
        reg_values[reg_index] = ir.GetGPR(reg);
    return reg_values[reg_index];
}

void ArmTranslator::SetReg(ArmReg reg, std::shared_ptr<MicroValue> value) {
    size_t reg_index = static_cast<size_t>(reg);
    reg_values[reg_index] = value;
}

void ArmTranslator::FallbackToInterpreter() {
    ir.SetTerm(MicroTerm::Interpret(current));
    stop_compilation = true;
}

bool ArmTranslator::ConditionPassed(Cond cond) {
    if (cond == current.cond && ir.flags_written == MicroArmFlags::None) {
        // TODO(merry): One can do more fine-grained checks on ir.flags_written.
        //              For example, if cond == GE, we only need to check N and V weren't written.
        return true;
    }

    // We didn't actually translate this instruction.
    instructions_translated--;

    auto next = current;
    next.cond = cond;
    ir.SetTerm(MicroTerm::LinkBlock(next));
    stop_compilation = true;
    return false;
}

void ArmTranslator::ALUWritePC(std::shared_ptr<MicroValue> new_pc) {
    BranchWritePC(new_pc); // ARMv6 behaviour
}

void ArmTranslator::LoadWritePC(std::shared_ptr<MicroValue> new_pc) {
    BXWritePC(new_pc); // ARMv6 behaviour
}

void ArmTranslator::BranchWritePC(u32 new_pc) {
    auto next = current;
    next.arm_pc = new_pc;
    ir.SetTerm(MicroTerm::LinkBlock(next));
    stop_compilation = true;
}

void ArmTranslator::BranchWritePC(std::shared_ptr<MicroValue> new_pc) {
    ir.Inst(MicroOp::BranchWritePC, new_pc);
    ir.SetTerm(MicroTerm::ReturnToDispatch());
    stop_compilation = true;
}

void ArmTranslator::BXWritePC(u32 new_pc) {
    auto next = current;
    next.TFlag = new_pc & 1;
    next.arm_pc = new_pc & (next.TFlag ? 0xFFFFFFFE : 0xFFFFFFFC);
    ir.SetTerm(MicroTerm::LinkBlock(next));
    stop_compilation = true;
}

void ArmTranslator::BXWritePC(std::shared_ptr<MicroValue> new_pc) {
    ir.Inst(MicroOp::BXWritePC, new_pc);
    ir.SetTerm(MicroTerm::ReturnToDispatch());
    stop_compilation = true;
}

u32 ArmTranslator::ArmExpandImm(u32 imm8, int rotate) {
    return _rotl(imm8, rotate * 2);
}

u32 ArmTranslator::PC() const {
    return current.arm_pc + 8;
}

// Branch instructions
void ArmTranslator::B(Cond cond, ArmImm24 imm24) {
    // Decode
    u32 imm32 = BitUtil::SignExtend<26, u32>(imm24 << 2);

    // Execute
    if (!ConditionPassed(cond))
        return;

    BranchWritePC(PC() + imm32);
}

void ArmTranslator::BL(Cond cond, ArmImm24 imm24) { FallbackToInterpreter(); }
void ArmTranslator::BLX_imm(bool H, ArmImm24 imm24) { FallbackToInterpreter(); }
void ArmTranslator::BLX_reg(Cond cond, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::BX(Cond cond, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::BXJ(Cond cond, ArmReg Rm_index) { FallbackToInterpreter(); }

// Coprocessor instructions
void ArmTranslator::CDP() { FallbackToInterpreter(); }
void ArmTranslator::LDC() { FallbackToInterpreter(); }
void ArmTranslator::MCR() { FallbackToInterpreter(); }
void ArmTranslator::MCRR() { FallbackToInterpreter(); }
void ArmTranslator::MRC() { FallbackToInterpreter(); }
void ArmTranslator::MRRC() { FallbackToInterpreter(); }
void ArmTranslator::STC() { FallbackToInterpreter(); }

// Data processing instructions
void ArmTranslator::ADC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::ADC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::ADC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }

void ArmTranslator::ADD_imm(Cond cond, bool S, ArmReg n, ArmReg d, int rotate, ArmImm8 imm8) {
    // Decode
    u32 expanded_imm = ArmExpandImm(imm8, rotate);
    MicroArmFlags write_flags = S ? MicroArmFlags::NZCV : MicroArmFlags::None;

    // Execute
    if (!ConditionPassed(cond))
        return;

    auto Rn = GetReg(n);
    auto imm32 = ir.ConstU32(expanded_imm);

    auto result = ir.Inst(MicroOp::Add, Rn, imm32, write_flags);

    if (d == ArmReg::PC) {
        ALUWritePC(result);
    } else {
        SetReg(d, result);
    }
}

void ArmTranslator::ADD_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::ADD_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::AND_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::AND_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::AND_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::BIC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::BIC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::BIC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::CMN_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::CMN_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::CMN_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::CMP_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::CMP_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::CMP_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::EOR_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::EOR_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::EOR_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::MOV_imm(Cond cond, bool S, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::MOV_reg(Cond cond, bool S, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::MOV_rsr(Cond cond, bool S, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::MVN_imm(Cond cond, bool S, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::MVN_reg(Cond cond, bool S, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::MVN_rsr(Cond cond, bool S, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::ORR_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::ORR_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::ORR_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::RSB_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::RSB_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::RSB_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::RSC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::RSC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::RSC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SBC_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::SBC_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SBC_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SUB_imm(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::SUB_reg(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SUB_rsr(Cond cond, bool S, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::TEQ_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::TEQ_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::TEQ_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::TST_imm(Cond cond, ArmReg Rn_index, int rotate, ArmImm8 imm8) { FallbackToInterpreter(); }
void ArmTranslator::TST_reg(Cond cond, ArmReg Rn_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::TST_rsr(Cond cond, ArmReg Rn_index, ArmReg Rs_index, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }

// Exception generation instructions
void ArmTranslator::BKPT(Cond cond, ArmImm12 imm12, ArmImm4 imm4) { FallbackToInterpreter(); }
void ArmTranslator::SVC(Cond cond, ArmImm24 imm24) { FallbackToInterpreter(); }
void ArmTranslator::UDF() { FallbackToInterpreter(); }

// Extension functions
void ArmTranslator::SXTAB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SXTAB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SXTAH(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SXTB(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SXTB16(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SXTH(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UXTAB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UXTAB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UXTAH(Cond cond, ArmReg Rn_index, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UXTB(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UXTB16(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UXTH(Cond cond, ArmReg Rd_index, SignExtendRotation rotate, ArmReg Rm_index) { FallbackToInterpreter(); }

// Hint instructions
void ArmTranslator::PLD() { FallbackToInterpreter(); }
void ArmTranslator::SEV() { FallbackToInterpreter(); }
void ArmTranslator::WFE() { FallbackToInterpreter(); }
void ArmTranslator::WFI() { FallbackToInterpreter(); }
void ArmTranslator::YIELD() { FallbackToInterpreter(); }

// Load/Store instructions
void ArmTranslator::LDR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::LDR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::LDRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::LDRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::LDRBT() { FallbackToInterpreter(); }
void ArmTranslator::LDRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::LDRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::LDRHT() { FallbackToInterpreter(); }
void ArmTranslator::LDRSB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRSB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::LDRSBT() { FallbackToInterpreter(); }
void ArmTranslator::LDRSH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::LDRSH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::LDRSHT() { FallbackToInterpreter(); }
void ArmTranslator::LDRT() { FallbackToInterpreter(); }
void ArmTranslator::STR_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::STR_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::STRB_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm11 imm11) { FallbackToInterpreter(); }
void ArmTranslator::STRB_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ShiftType shift, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::STRBT() { FallbackToInterpreter(); }
void ArmTranslator::STRD_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::STRD_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::STRH_imm(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmImm4 imm8a, ArmImm4 imm8b) { FallbackToInterpreter(); }
void ArmTranslator::STRH_reg(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::STRHT() { FallbackToInterpreter(); }
void ArmTranslator::STRT() { FallbackToInterpreter(); }

// Load/Store multiple instructions
void ArmTranslator::LDM(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmRegList list) { FallbackToInterpreter(); }
void ArmTranslator::LDM_usr() { FallbackToInterpreter(); }
void ArmTranslator::LDM_eret() { FallbackToInterpreter(); }
void ArmTranslator::STM(Cond cond, bool P, bool U, bool W, ArmReg Rn_index, ArmRegList list) { FallbackToInterpreter(); }
void ArmTranslator::STM_usr() { FallbackToInterpreter(); }

// Miscellaneous instructions
void ArmTranslator::CLZ(Cond cond, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::NOP() { FallbackToInterpreter(); }
void ArmTranslator::SEL(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }

// Unsigned sum of absolute difference functions
void ArmTranslator::USAD8(Cond cond, ArmReg Rd_index, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::USADA8(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }

// Packing instructions
void ArmTranslator::PKHBT(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::PKHTB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmImm5 imm5, ArmReg Rm_index) { FallbackToInterpreter(); }

// Reversal instructions
void ArmTranslator::REV(Cond cond, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::REV16(Cond cond, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::REVSH(Cond cond, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }

// Saturation instructions
void ArmTranslator::SSAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd_index, ArmImm5 imm5, bool sh, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SSAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd_index, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::USAT(Cond cond, ArmImm5 sat_imm, ArmReg Rd_index, ArmImm5 imm5, bool sh, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::USAT16(Cond cond, ArmImm4 sat_imm, ArmReg Rd_index, ArmReg Rn_index) { FallbackToInterpreter(); }

// Multiply (Normal) instructions
void ArmTranslator::MLA(Cond cond, bool S, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::MUL(Cond cond, bool S, ArmReg Rd_index, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }

// Multiply (Long) instructions
void ArmTranslator::SMLAL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMULL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::UMAAL(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::UMLAL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::UMULL(Cond cond, bool S, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, ArmReg Rn_index) { FallbackToInterpreter(); }

// Multiply (Halfword) instructions
void ArmTranslator::SMLALxy(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, bool M, bool N, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMLAxy(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, bool N, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMULxy(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, bool N, ArmReg Rn_index) { FallbackToInterpreter(); }

// Multiply (word by halfword) instructions
void ArmTranslator::SMLAWy(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMULWy(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }

// Multiply (Most significant word) instructions
void ArmTranslator::SMMLA(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool R, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMMLS(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool R, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMMUL(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool R, ArmReg Rn_index) { FallbackToInterpreter(); }

// Multiply (Dual) instructions
void ArmTranslator::SMLAD(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMLALD(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMLSD(Cond cond, ArmReg Rd_index, ArmReg Ra, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMLSLD(Cond cond, ArmReg Rd_indexHi, ArmReg Rd_indexLo, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMUAD(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }
void ArmTranslator::SMUSD(Cond cond, ArmReg Rd_index, ArmReg Rm_index, bool M, ArmReg Rn_index) { FallbackToInterpreter(); }

// Parallel Add/Subtract (Modulo arithmetic) instructions
void ArmTranslator::SADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::USAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::USUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::USUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }

// Parallel Add/Subtract (Saturating) instructions
void ArmTranslator::QADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UQADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UQADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UQASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UQSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UQSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UQSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }

// Parallel Add/Subtract (Halving) instructions
void ArmTranslator::SHADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SHADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SHASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SHSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SHSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SHSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UHADD8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UHADD16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UHASX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UHSAX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UHSUB8(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::UHSUB16(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }

// Saturated Add/Subtract instructions
void ArmTranslator::QADD(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QSUB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QDADD(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::QDSUB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }

// Synchronization Primitive instructions
void ArmTranslator::CLREX() { FallbackToInterpreter(); }
void ArmTranslator::LDREX(Cond cond, ArmReg Rn_index, ArmReg Rd_index) { FallbackToInterpreter(); }
void ArmTranslator::LDREXB(Cond cond, ArmReg Rn_index, ArmReg Rd_index) { FallbackToInterpreter(); }
void ArmTranslator::LDREXD(Cond cond, ArmReg Rn_index, ArmReg Rd_index) { FallbackToInterpreter(); }
void ArmTranslator::LDREXH(Cond cond, ArmReg Rn_index, ArmReg Rd_index) { FallbackToInterpreter(); }
void ArmTranslator::STREX(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::STREXB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::STREXD(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::STREXH(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SWP(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }
void ArmTranslator::SWPB(Cond cond, ArmReg Rn_index, ArmReg Rd_index, ArmReg Rm_index) { FallbackToInterpreter(); }

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

} // namespace ArmJit
