#include "core/arm/jit/ir_builder.h"

namespace ArmJit {

std::shared_ptr<MicroValue> MicroBuilder::GetGPR(ArmReg reg) {
    std::shared_ptr<MicroValue> ret = std::make_shared<MicroGetGPR>(reg);
    block.instructions.emplace_back(ret);
    return ret;
}

std::shared_ptr<ArmJit::MicroValue> MicroBuilder::ConstU32(u32 value) {
    std::shared_ptr<MicroValue> ret = std::make_shared<MicroConstU32>(value);
    block.instructions.emplace_back(ret);
    return ret;
}

std::shared_ptr<MicroValue> MicroBuilder::Inst(MicroOp op, std::shared_ptr<MicroValue> a) {
    std::shared_ptr<MicroValue> ret = std::make_shared<MicroInst>(op, std::initializer_list<std::shared_ptr<MicroValue>>{a});
    block.instructions.emplace_back(ret);
    return ret;
}

std::shared_ptr<MicroValue> MicroBuilder::Inst(MicroOp op, std::shared_ptr<MicroValue> a, std::shared_ptr<MicroValue> b) {
    std::shared_ptr<MicroValue> ret = std::make_shared<MicroInst>(op, std::initializer_list<std::shared_ptr<MicroValue>>{a, b});
    block.instructions.emplace_back(ret);
    return ret;
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
