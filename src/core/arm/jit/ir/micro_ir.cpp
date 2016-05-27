// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>

#include "common/assert.h"

#include "core/arm/jit/ir/micro_ir.h"
#include "core/arm/jit/ir/micro_ops.h"

namespace ArmJit {

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

// MicroSetGPR class member definitions

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

void MicroSetGPR::ReplaceUseOfXWithY(std::shared_ptr<MicroValue> x, std::shared_ptr<MicroValue> y) {
    bool has_use = false;
    auto this_ = shared_from_this();

    if (arg.lock() == x) {
        arg = y;
        x->RemoveUse(this_);
        y->AddUse(this_);
        has_use = true;
    }

    ASSERT_MSG(has_use, "ReplaceUseOfXWithY: This MicroSetGPR doesn't have x. Bug in use management code.");
}

// MicroInst class member definitions

MicroInst::MicroInst(MicroOp op_)
    : op(op_), write_flags(GetMicroOpInfo(op).default_write_flags)
{
    args.resize(GetMicroOpInfo(op).NumArgs());
}

MicroType MicroInst::GetType() const {
    return GetMicroOpInfo(op).ret_type;
}

size_t MicroInst::NumArgs() const {
    return GetMicroOpInfo(op).NumArgs();
}

void MicroInst::SetArg(size_t index, std::shared_ptr<MicroValue> value) {
    auto this_ = shared_from_this();

    if (auto prev_value = args.at(index).lock()) {
        prev_value->RemoveUse(this_);
    }

    ASSERT(value->GetType() == GetMicroOpInfo(op).arg_types.at(index));
    args.at(index) = value;

    value->AddUse(this_);
}

std::shared_ptr<MicroValue> MicroInst::GetArg(size_t index) const {
    ASSERT_MSG(!args.at(index).expired(), "This should never happen. All MicroValues should be owned by a MicroBlock.");
    return args.at(index).lock();
}

MicroArmFlags MicroInst::ReadFlags() const {
    return GetMicroOpInfo(op).read_flags;
}

MicroArmFlags MicroInst::WriteFlags() const {
    return write_flags;
}

void MicroInst::AssertValid() {
    ASSERT(std::all_of(args.begin(), args.end(), [](const auto& arg) { return !arg.expired(); }));
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