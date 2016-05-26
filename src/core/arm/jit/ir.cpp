// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <map>
#include <type_traits>

#include "common/assert.h"

#include "core/arm/jit/ir.h"

namespace ArmJit {

namespace OpTable {

using Op = MicroOp;
using Type = MicroType;
using Flag = MicroArmFlags;

const std::map<MicroOp, const MicroOpInfo> micro_op_info {{
    { Op::ConstU32,     { Op::ConstU32,     Type::U32,  Flag::None, Flag::None, {} } },
    { Op::GetGPR,       { Op::GetGPR,       Type::U32,  Flag::None, Flag::None, {} } },
    { Op::Add,          { Op::Add,          Type::U32,  Flag::None, Flag::NZCV, { Type::U32, Type::U32 } } },
    { Op::AddWithCarry, { Op::AddWithCarry, Type::U32,  Flag::C,    Flag::NZCV, { Type::U32, Type::U32 } } },
}};

} // namespace OpTable

using OpTable::micro_op_info;

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

// MicroValue class member definitions

void MicroValue::ReplaceUsesWith(std::shared_ptr<MicroValue> replacement) {
    for (const auto& use : uses) {
        use.use_owner.lock()->ReplaceUseOfXWithY(use.value.lock(), replacement);
    }
    ASSERT(uses.empty());
}

void MicroValue::AddUse(std::shared_ptr<MicroValue> owner) {
    // There can be multiple uses from the same owner.
    uses.push_back({ shared_from_this(), owner });
}

void MicroValue::RemoveUse(std::shared_ptr<MicroValue> owner) {
    // Remove only one use.
    auto iter = std::find_if(uses.begin(), uses.end(), [&](auto use) { return use.use_owner.lock() == owner; });
    ASSERT_MSG(iter != uses.end(), "RemoveUse: RemoveUse without associated AddUse. Bug in use management code.");
    uses.erase(iter);
}

void MicroValue::ReplaceUseOfXWithY(std::shared_ptr<MicroValue> x, std::shared_ptr<MicroValue> y) {
    // This should never be called. Use management is incorrect if this is ever called.
    ASSERT_MSG(false, "ReplaceUseOfXWithY: This MicroValue type doesn't use any values. Bug in use management code.");
}

// MicroSetGPR

void MicroSetGPR::SetArg(std::shared_ptr<MicroValue> value) {
    auto this_ = shared_from_this();

    if (auto prev_value = arg.lock()) {
        prev_value->RemoveUse(this_);
    }

    ASSERT(value->GetType() == MicroType::U32);
    arg = value;

    value->AddUse(this_);
}

std::shared_ptr<MicroValue> MicroSetGPR::GetArg() const {
    ASSERT(!arg.expired());
    return arg.lock();
}


// MicroInst class member definitions}

MicroInst::MicroInst(MicroOp op_, std::initializer_list<std::shared_ptr<MicroValue>> values)
    : op(op_), write_flags(micro_op_info.at(op).default_write_flags)
{
    ASSERT(micro_op_info.at(op).NumArgs() == values.size());
    args.resize(values.size());

    size_t index = 0;
    for (auto& value : values) {
        SetArg(index++, value);
    }
}

MicroType MicroInst::GetType() const {
    return micro_op_info.at(op).ret_type;
}

size_t MicroInst::NumArgs() const {
    return micro_op_info.at(op).NumArgs();
}

void MicroInst::SetArg(size_t index, std::shared_ptr<MicroValue> value) {
    auto this_ = shared_from_this();

    if (auto prev_value = args.at(index).lock()) {
        prev_value->RemoveUse(this_);
    }

    ASSERT(value->GetType() == micro_op_info.at(op).types.at(index));
    args.at(index) = value;

    value->AddUse(this_);
}

std::shared_ptr<MicroValue> MicroInst::GetArg(size_t index) const {
    ASSERT(!args.at(index).expired());
    return args.at(index).lock();
}

MicroArmFlags MicroInst::ReadFlags() const {
    return micro_op_info.at(op).read_flags;
}

MicroArmFlags MicroInst::WriteFlags() const {
    return write_flags;
}

void MicroInst::ReplaceUseOfXWithY(std::shared_ptr<MicroValue> x, std::shared_ptr<MicroValue> y) {
    bool has_use = false;
    auto this_ = shared_from_this();

    // Note that there may be multiple uses of x.
    for (auto& arg : args) {
        if (arg.lock() == x) {
            arg = y;
            has_use = true;
            x->RemoveUse(this_);
            y->AddUse(this_);
        }
    }

    ASSERT_MSG(has_use, "ReplaceUseOfXWithY: This MicroInst doesn't have x. Bug in use management code.");
}

} // namespace ArmJit