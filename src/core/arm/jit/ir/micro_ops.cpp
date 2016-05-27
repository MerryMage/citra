#include <map>
#include <type_traits>

#include "core/arm/jit/ir/micro_ops.h"

namespace ArmJit {

using Op = MicroOp;
using Type = MicroType;
using Flag = MicroArmFlags;

const std::map<MicroOp, const MicroOpInfo> micro_op_info {{
    // Basic load/stores
    { Op::ConstU32,     { Op::ConstU32,     Type::U32,  Flag::None, Flag::None, {} } },
    { Op::GetGPR,       { Op::GetGPR,       Type::U32,  Flag::None, Flag::None, {} } },
    { Op::SetGPR,       { Op::SetGPR,       Type::Void, Flag::None, Flag::None, { Type::U32 } } },

    // ARM ALU
    { Op::Add,          { Op::Add,          Type::U32,  Flag::None, Flag::NZCV, { Type::U32, Type::U32 } } },
    { Op::AddWithCarry, { Op::AddWithCarry, Type::U32,  Flag::C,    Flag::NZCV, { Type::U32, Type::U32 } } },
}};

MicroOpInfo GetMicroOpInfo(MicroOp op) {
    return micro_op_info.at(op);
}

// MicroArmFlags free functions

MicroArmFlags operator~(MicroArmFlags a) {
    using underlyingT = std::underlying_type_t<MicroArmFlags>;
    return static_cast<MicroArmFlags>(~static_cast<underlyingT>(a));
}

MicroArmFlags operator|(MicroArmFlags a, MicroArmFlags b) {
    using underlyingT = std::underlying_type_t<MicroArmFlags>;
    return static_cast<MicroArmFlags>(static_cast<underlyingT>(a) | static_cast<underlyingT>(b));
}

MicroArmFlags operator&(MicroArmFlags a, MicroArmFlags b) {
    using underlyingT = std::underlying_type_t<MicroArmFlags>;
    return static_cast<MicroArmFlags>(static_cast<underlyingT>(a) & static_cast<underlyingT>(b));
}

} // namespace ArmJit