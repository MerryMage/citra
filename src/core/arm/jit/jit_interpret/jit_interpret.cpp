// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/bit_field.h"

#include "core/arm/jit/jit_common.h"
#include "core/arm/jit/jit_interpret/jit_interpret.h"
#include "core/arm/jit/translate/translate.h"
#include "core/arm/skyeye_common/armstate.h"
#include "core/core.h"
#include "core/core_timing.h"

#include <core/arm/jit/ir/micro_ir.h>
#include <map>
#include <boost/variant/get.hpp>

namespace ArmJit {
namespace Interpret {

struct TACInst {
    u16 opcode;
    u16 dest;
    union {
        struct {
            u16 a;
            u16 b;
        } args;
        u32 imm32;
    };
};
static_assert(sizeof(TACInst) == sizeof(u64), "TACInst should fit in a u64");

struct TACBlock {
    std::vector<TACInst> instructions;
    MicroTerminal terminal;
    size_t cycles_consumed = 0;
};

struct TACRunState {
    std::array<u32, 65535> regs;
    Cond cond;
};

TACBlock TranslateToTAC(const LocationDescriptor& desc) {
    MicroBlock micro_block = Translate(desc);
    TACBlock tac_block;

    tac_block.terminal = micro_block.terminal;
    tac_block.cycles_consumed = micro_block.cycles_consumed;

    u16 free_pos = 16;
    std::map<MicroValue*, u16> micro_value_to_pos;
    for (const auto& micro_inst : micro_block.instructions) {
        TACInst inst;
        inst.opcode = static_cast<u16>(micro_inst->GetOp());

        switch (micro_inst->GetOp()) {
        case MicroOp::GetGPR: {
            auto real_inst = std::static_pointer_cast<MicroGetGPR>(micro_inst);
            inst.dest = free_pos;
            micro_value_to_pos[micro_inst.get()] = free_pos++;
            inst.args.a = static_cast<u16>(real_inst->reg);
            break;
        }
        case MicroOp::SetGPR: {
            auto real_inst = std::static_pointer_cast<MicroSetGPR>(micro_inst);
            inst.args.a = static_cast<u16>(real_inst->reg);
            inst.args.b = micro_value_to_pos.at(real_inst->GetArg().get());
            break;
        }
        case MicroOp::ConstU32: {
            auto real_inst = std::static_pointer_cast<MicroConstU32>(micro_inst);
            inst.dest = free_pos;
            micro_value_to_pos[micro_inst.get()] = free_pos++;
            inst.imm32 = real_inst->value;
            break;
        }
        default: {
            auto real_inst = std::static_pointer_cast<MicroInst>(micro_inst);
            if (real_inst->GetType() != MicroType::Void) {
                inst.dest = free_pos;
                micro_value_to_pos[micro_inst.get()] = free_pos++;
            }
            if (real_inst->NumArgs() >= 1)
                inst.args.a = micro_value_to_pos[real_inst->GetArg(0).get()];
            if (real_inst->NumArgs() >= 2)
                inst.args.b = micro_value_to_pos[real_inst->GetArg(1).get()];
            ASSERT(real_inst->NumArgs() <= 2);
            if (real_inst->WriteFlags() != MicroArmFlags::None) {
                inst.opcode |= 0x8000;
            }
            break;
        }
        }

        tac_block.instructions.emplace_back(inst);
    }

    return tac_block;
}

void RunTAC(ARMul_State& cpu_state, TACRunState& state, const TACBlock& block) {
    std::memcpy(state.regs.data(), cpu_state.Reg.data(), 16 * sizeof(u32));

    bool TFlag = cpu_state.Cpsr & (1 << 5);
    bool EFlag = cpu_state.Cpsr & (1 << 9);
    bool NFlag = cpu_state.Cpsr & (1 << 31);
    bool ZFlag = cpu_state.Cpsr & (1 << 30);
    bool CFlag = cpu_state.Cpsr & (1 << 29);
    bool VFlag = cpu_state.Cpsr & (1 << 28);

    u32* regs = state.regs.data();

    for (auto inst : block.instructions) {
        bool write_flags = inst.opcode & 0x8000;
        switch (static_cast<MicroOp>(inst.opcode & 0x7FFF)) {
        case MicroOp::GetGPR:
            regs[inst.dest] = regs[inst.args.a];
            break;
        case MicroOp::SetGPR:
            regs[inst.args.a] = regs[inst.args.b];
            break;
        case MicroOp::ConstU32:
            regs[inst.dest] = inst.imm32;
            break;
        case MicroOp::Add:
            regs[inst.dest] = regs[inst.args.a] + regs[inst.args.b];
            if (write_flags) {
                NFlag = regs[inst.dest] & 0x80000000;
                ZFlag = regs[inst.dest] == 0;
                CFlag = regs[inst.dest] < regs[inst.args.a];
                VFlag = ((regs[inst.args.a] & 0x80000000) == (regs[inst.args.b] & 0x80000000)) && ((regs[inst.dest] & 0x80000000) != (regs[inst.args.a] & 0x80000000));
            }
            break;
        default:
            ASSERT(false); // Oops.
        }
    }

    if (block.terminal.type() == typeid(MicroTerm::PopRSBHint) || block.terminal.type() == typeid(MicroTerm::ReturnToDispatch)) {
        state.cond = Cond::AL;
    } else if (block.terminal.type() == typeid(MicroTerm::LinkBlock)) {
        auto link = boost::get<MicroTerm::LinkBlock>(block.terminal);
        regs[15] = link.next.arm_pc;
        TFlag = link.next.TFlag;
        EFlag = link.next.EFlag;
        state.cond = link.next.cond;
    } else if (block.terminal.type() == typeid(MicroTerm::LinkBlockFast)) {
        auto link = boost::get<MicroTerm::LinkBlock>(block.terminal);
        regs[15] = link.next.arm_pc;
        TFlag = link.next.TFlag;
        EFlag = link.next.EFlag;
        state.cond = link.next.cond;
    } else if (block.terminal.type() == typeid(MicroTerm::Interpret)) {
        ASSERT(false); // Unimplemented for now.
    } else {
        ASSERT(false); // Oops.
    }

    cpu_state.Cpsr &= ~((1 << 5) | (1 << 9) | (1 << 31) | (1 << 30) | (1 << 29) | (1 << 28));
    if (TFlag) cpu_state.Cpsr |= (1 << 5);
    if (EFlag) cpu_state.Cpsr |= (1 << 9);
    if (NFlag) cpu_state.Cpsr |= (1 << 31);
    if (ZFlag) cpu_state.Cpsr |= (1 << 30);
    if (CFlag) cpu_state.Cpsr |= (1 << 29);
    if (VFlag) cpu_state.Cpsr |= (1 << 28);

    std::memcpy(cpu_state.Reg.data(), state.regs.data(), 16 * sizeof(u32));
}

struct ARM_MicroInterpreter::Impl {
    Impl() : cpu_state(PrivilegeMode::USER32MODE) {}

    //TODO(merry): Eventually get rid of the skyeye dependency
    ARMul_State cpu_state;
    bool reschedule = false;

    TACRunState tac_state;
    std::unordered_map<LocationDescriptor, TACBlock, LocationDescriptorHash> tac_cache;
};

ARM_MicroInterpreter::ARM_MicroInterpreter(PrivilegeMode initial_mode) : impl(std::make_unique<Impl>()) {
    ASSERT_MSG(initial_mode == PrivilegeMode::USER32MODE, "Unimplemented");
    ClearCache();
}

ARM_MicroInterpreter::~ARM_MicroInterpreter() {
}

void ARM_MicroInterpreter::SetPC(u32 pc) {
    impl->cpu_state.Reg[15] = pc;
}

u32 ARM_MicroInterpreter::GetPC() const {
    return impl->cpu_state.Reg[15];
}

u32 ARM_MicroInterpreter::GetReg(int index) const {
    if (index == 15) return GetPC();
    return impl->cpu_state.Reg[index];
}

void ARM_MicroInterpreter::SetReg(int index, u32 value) {
    if (index == 15) return SetPC(value);
    impl->cpu_state.Reg[index] = value;
}

u32 ARM_MicroInterpreter::GetVFPReg(int index) const {
    return impl->cpu_state.ExtReg[index];
}

void ARM_MicroInterpreter::SetVFPReg(int index, u32 value) {
    impl->cpu_state.ExtReg[index] = value;
}

u32 ARM_MicroInterpreter::GetVFPSystemReg(VFPSystemRegister reg) const {
    return impl->cpu_state.VFP[reg];
}

void ARM_MicroInterpreter::SetVFPSystemReg(VFPSystemRegister reg, u32 value) {
    impl->cpu_state.VFP[reg] = value;
}

u32 ARM_MicroInterpreter::GetCPSR() const {
    return impl->cpu_state.Cpsr;
}

void ARM_MicroInterpreter::SetCPSR(u32 cpsr) {
    impl->cpu_state.Cpsr = cpsr;
}

u32 ARM_MicroInterpreter::GetCP15Register(CP15Register reg) {
    return impl->cpu_state.CP15[reg];
}

void ARM_MicroInterpreter::SetCP15Register(CP15Register reg, u32 value) {
    impl->cpu_state.CP15[reg] = value;
}

void ARM_MicroInterpreter::AddTicks(u64 ticks) {
    down_count -= ticks;
    if (down_count < 0)
        CoreTiming::Advance();
}

void ARM_MicroInterpreter::ExecuteInstructions(int num_instructions) {
    impl->reschedule = false;

    do {
        u32 arm_pc = impl->cpu_state.Reg[15];
        bool TFlag = (impl->cpu_state.Cpsr >> 5) & 1;
        bool EFlag = (impl->cpu_state.Cpsr >> 9) & 1;
        LocationDescriptor desc { arm_pc, TFlag, EFlag, impl->tac_state.cond };

        auto iter = impl->tac_cache.find(desc);
        if (iter != impl->tac_cache.end()) {
            const auto& tac_block = iter->second;
            RunTAC(impl->cpu_state, impl->tac_state, tac_block);
            num_instructions -= tac_block.cycles_consumed;
        } else {
            TACBlock tac_block = TranslateToTAC(desc);
            impl->tac_cache[desc] = tac_block;
            RunTAC(impl->cpu_state, impl->tac_state, tac_block);
            num_instructions -= tac_block.cycles_consumed;
        }
    } while (!impl->reschedule && num_instructions > 0);
}

void ARM_MicroInterpreter::ResetContext(Core::ThreadContext& context, u32 stack_top, u32 entry_point, u32 arg) {
    memset(&context, 0, sizeof(Core::ThreadContext));

    context.cpu_registers[0] = arg;
    context.pc = entry_point;
    context.sp = stack_top;
    context.cpsr = 0x1F; // Usermode
}

void ARM_MicroInterpreter::SaveContext(Core::ThreadContext& ctx) {
    memcpy(ctx.cpu_registers, impl->cpu_state.Reg.data(), sizeof(ctx.cpu_registers));
    memcpy(ctx.fpu_registers, impl->cpu_state.ExtReg.data(), sizeof(ctx.fpu_registers));

    ctx.sp = impl->cpu_state.Reg[13];
    ctx.lr = impl->cpu_state.Reg[14];
    ctx.pc = impl->cpu_state.Reg[15];

    ctx.cpsr = GetCPSR();

    ctx.fpscr = impl->cpu_state.VFP[1];
    ctx.fpexc = impl->cpu_state.VFP[2];
}

void ARM_MicroInterpreter::LoadContext(const Core::ThreadContext& ctx) {
    memcpy(impl->cpu_state.Reg.data(), ctx.cpu_registers, sizeof(ctx.cpu_registers));
    memcpy(impl->cpu_state.ExtReg.data(), ctx.fpu_registers, sizeof(ctx.fpu_registers));

    impl->cpu_state.Reg[13] = ctx.sp;
    impl->cpu_state.Reg[14] = ctx.lr;
    impl->cpu_state.Reg[15] = ctx.pc;
    SetCPSR(ctx.cpsr);

    impl->cpu_state.VFP[1] = ctx.fpscr;
    impl->cpu_state.VFP[2] = ctx.fpexc;
}

void ARM_MicroInterpreter::PrepareReschedule() {
    impl->reschedule = true;
    impl->cpu_state.NumInstrsToExecute = 0;
}

void ARM_MicroInterpreter::ClearCache() {
    impl->cpu_state.instruction_cache.clear();
    impl->tac_cache.clear();
}

} // namespace Interpret
} // namespace ArmJit
