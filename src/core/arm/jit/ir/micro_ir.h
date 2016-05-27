// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <vector>

#include <boost/variant/recursive_wrapper.hpp>
#include <boost/variant/variant.hpp>

#include "common/common_types.h"

#include "core/arm/jit/jit_common.h"
#include "core/arm/jit/ir/micro_ops.h"

namespace ArmJit {

// ARM JIT Microinstruction Intermediate Representation
//
// This intermediate representation is an SSA IR. It is designed primarily for analysis,
// though it can be lowered into a reduced form for interpretation. Each IR node (MicroValue)
// is a microinstruction of an idealised ARM CPU. The choice of microinstructions is made
// not based on any existing microarchitecture but on ease of implementation and future
// optimization work.
//
// A basic block is represented as a MicroBlock.

// Forward declarations

class MicroBlock;
class MicroConstU32;
class MicroGetGPR;
class MicroInst;
class MicroSetGPR;
class MicroValue;

// MicroTerminal declarations

namespace MicroTerm {
    struct If;

    /**
     * This terminal instruction calls the interpreter, starting at `next`.
     * The interpreter must interpret at least 1 instruction but may choose to interpret more.
     */
    struct Interpret {
        explicit Interpret(const LocationDescriptor& next_) : next(next_) {}
        LocationDescriptor next; ///< Location at which interpretation starts.
    };

    /**
     * This terminal instruction returns control to the dispatcher.
     * The dispatcher will use the value in R15 to determine what comes next.
     */
    struct ReturnToDispatch {};

    /**
     * This terminal instruction jumps to the basic block described by `next` if we have enough
     * cycles remaining. If we do not have enough cycles remaining, we return to the
     * dispatcher, which will return control to the host.
     */
    struct LinkBlock {
        explicit LinkBlock(const LocationDescriptor& next_) : next(next_) {}
        LocationDescriptor next; ///< Location descriptor for next block.
    };

    /**
     * This terminal instruction jumps to the basic block described by `next` unconditionally.
     * This is an optimization and MUST only be emitted when this is guaranteed not to result
     * in hanging, even in the face of other optimizations. (In practice, this means that only
     * forward jumps to short-ish blocks would use this instruction.)
     * A backend that doesn't support this optimization may choose to implement this exactly
     * as LinkBlock.
     */
    struct LinkBlockFast {
        explicit LinkBlockFast(const LocationDescriptor& next_) : next(next_) {}
        LocationDescriptor next; ///< Location descriptor for next block.
    };

    /**
     * This terminal instruction checks the top of the Return Stack Buffer against R15.
     * If RSB lookup fails, control is returned to the dispatcher.
     * This is an optimization for faster function calls. A backend that doesn't support
     * this optimization or doesn't have a RSB may choose to implement this exactly as
     * ReturnToDispatch.
     */
    struct PopRSBHint {};

    /// A MicroTerminal is the terminal instruction in a MicroBlock.
    using MicroTerminal = boost::variant<
        ReturnToDispatch,
        PopRSBHint,
        Interpret,
        LinkBlock,
        LinkBlockFast,
        boost::recursive_wrapper<If>
    >;

    /**
     * This terminal instruction conditionally executes one terminal or another depending
     * on the run-time state of the ARM flags.
     */
    struct If {
        If(Cond if__, MicroTerminal then__, MicroTerminal else__) : if_(if__), then_(then__), else_(else__) {}
        Cond if_;
        MicroTerminal then_;
        MicroTerminal else_;
    };
} // namespace MicroTerm

using MicroTerm::MicroTerminal;

// Type declarations

/// Base class for microinstructions to derive from.
class MicroValue : protected std::enable_shared_from_this<MicroValue> {
public:
    virtual ~MicroValue() = default;

    bool HasUses() const { return !uses.empty(); }
    bool HasOneUse() const { return uses.size() == 1; }
    bool HasManyUses() const { return uses.size() > 1; }

    /// Replace all uses of this MicroValue with `replacement`.
    void ReplaceUsesWith(std::shared_ptr<MicroValue> replacement);

    /// Get the microop this microinstruction represents.
    virtual MicroOp GetOp() const = 0;
    /// Get the type this instruction returns.
    virtual MicroType GetType() const = 0;
    /// Get the number of arguments this instruction has.
    virtual size_t NumArgs() const { return 0; }
    /// Gets the flags this instruction reads.
    virtual MicroArmFlags ReadFlags() const { return MicroArmFlags::None; }
    /// Gets the flags this instruction writes.
    virtual MicroArmFlags WriteFlags() const { return MicroArmFlags::None; }

protected:
    friend class MicroInst;
    friend class MicroSetGPR;

    void AddUse(std::shared_ptr<MicroValue> owner);
    void RemoveUse(std::shared_ptr<MicroValue> owner);
    virtual void ReplaceUseOfXWithY(std::shared_ptr<MicroValue> x, std::shared_ptr<MicroValue> y);

private:
    struct Use {
        /// The instruction which is being used.
        std::weak_ptr<MicroValue> value;
        /// The instruction which is using `value`.
        std::weak_ptr<MicroValue> use_owner;
    };

    std::list<Use> uses;
};

/// Representation of a u32 const load instruction.
class MicroConstU32 final : public MicroValue {
public:
    explicit MicroConstU32(u32 value_) : value(value_) {}
    ~MicroConstU32() override = default;

    MicroOp GetOp() const override { return MicroOp::ConstU32; }
    MicroType GetType() const override { return MicroType::U32; }

    const u32 value; ///< Literal value to load
};

/// Representation of a GPR load instruction.
class MicroGetGPR final : public MicroValue {
public:
    explicit MicroGetGPR(ArmReg reg_) : reg(reg_) {}
    ~MicroGetGPR() override = default;

    MicroOp GetOp() const override { return MicroOp::GetGPR; }
    MicroType GetType() const override { return MicroType::U32; }

    const ArmReg reg; ///< ARM register to load value from
};

/// Representation of a GPR store instruction.
class MicroSetGPR final : public MicroValue {
public:
    MicroSetGPR(ArmReg reg_) : reg(reg_) {}
    ~MicroSetGPR() override = default;

    MicroOp GetOp() const override { return MicroOp::SetGPR; }
    MicroType GetType() const override { return MicroType::Void; }
    size_t NumArgs() const override { return 1; }

    /// Set value to store in register.
    void SetArg(std::shared_ptr<MicroValue> value);
    /// Get value to store in register.
    std::shared_ptr<MicroValue> GetArg() const;

    ArmReg reg; ///< ARM register to store value to.

protected:
    void ReplaceUseOfXWithY(std::shared_ptr<MicroValue> x, std::shared_ptr<MicroValue> y) override;
private:
    std::weak_ptr<MicroValue> arg;
};

/**
 * A representation of a microinstruction. A single ARM/Thumb instruction may be
 * converted into zero or more microinstructions.
 */
class MicroInst final : public MicroValue {
public:
    MicroInst(MicroOp op);
    ~MicroInst() override = default;

    MicroOp GetOp() const override { return op; }
    MicroType GetType() const override;

    /// Get number of arguments.
    size_t NumArgs() const override;
    /// Set argument number `index` to `value`.
    void SetArg(size_t index, std::shared_ptr<MicroValue> value);
    /// Get argument number `index`.
    std::shared_ptr<MicroValue> GetArg(size_t index) const;

    MicroArmFlags ReadFlags() const override;
    MicroArmFlags WriteFlags() const override;
    void SetWriteFlags(MicroArmFlags flags) { write_flags = flags; }

    void AssertValid();

protected:
    void ReplaceUseOfXWithY(std::shared_ptr<MicroValue> x, std::shared_ptr<MicroValue> y) override;

private:
    MicroOp op;
    std::vector<std::weak_ptr<MicroValue>> args;
    MicroArmFlags write_flags;
};

/**
 * A basic block. It consists of zero or more instructions followed by exactly one terminal.
 * Note that this is a linear IR and not a pure tree-based IR: i.e.: there is an ordering to
 * the microinstructions and they may not be executed in an arbitrary order according to the
 * tree structure. This is important for correct ordering of reads to and writes from flags.
 */
class MicroBlock final {
public:
    explicit MicroBlock(const LocationDescriptor& location) : location(location) {}

    LocationDescriptor location;
    std::list<std::shared_ptr<MicroValue>> instructions;
    MicroTerminal terminal;
    size_t cycles_consumed = 0;
};

} // namespace ArmJit
