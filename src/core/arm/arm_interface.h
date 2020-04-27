// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/version.hpp>
#include "common/common_types.h"
#include "core/arm/arm_thread_context.h"
#include "core/arm/skyeye_common/arm_regformat.h"
#include "core/arm/skyeye_common/vfp/asm_vfp.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/memory.h"

namespace Memory {
class PageTable;
};

/// Generic ARM11 CPU interface
class ARM_Interface : NonCopyable {
public:
    explicit ARM_Interface(u32 id, std::shared_ptr<Core::Timing::Timer> timer)
        : timer(timer), id(id){};
    virtual ~ARM_Interface() {}

    /// Runs the CPU until an event happens
    virtual void Run() = 0;

    /// Step CPU by one instruction
    virtual void Step() = 0;

    /// Clear all instruction cache
    virtual void ClearInstructionCache() = 0;

    /**
     * Invalidate the code cache at a range of addresses.
     * @param start_address The starting address of the range to invalidate.
     * @param length The length (in bytes) of the range to invalidate.
     */
    virtual void InvalidateCacheRange(u32 start_address, std::size_t length) = 0;

    /// Notify CPU emulation that page tables have changed
    virtual void SetPageTable(const std::shared_ptr<Memory::PageTable>& page_table) = 0;

    /**
     * Set the Program Counter to an address
     * @param addr Address to set PC to
     */
    virtual void SetPC(u32 addr) = 0;

    /*
     * Get the current Program Counter
     * @return Returns current PC
     */
    virtual u32 GetPC() const = 0;

    /**
     * Get an ARM register
     * @param index Register index (0-15)
     * @return Returns the value in the register
     */
    virtual u32 GetReg(int index) const = 0;

    /**
     * Set an ARM register
     * @param index Register index (0-15)
     * @param value Value to set register to
     */
    virtual void SetReg(int index, u32 value) = 0;

    /**
     * Gets the value of a VFP register
     * @param index Register index (0-31)
     * @return Returns the value in the register
     */
    virtual u32 GetVFPReg(int index) const = 0;

    /**
     * Sets a VFP register to the given value
     * @param index Register index (0-31)
     * @param value Value to set register to
     */
    virtual void SetVFPReg(int index, u32 value) = 0;

    /**
     * Gets the current value within a given VFP system register
     * @param reg The VFP system register
     * @return The value within the VFP system register
     */
    virtual u32 GetVFPSystemReg(VFPSystemRegister reg) const = 0;

    /**
     * Sets the VFP system register to the given value
     * @param reg   The VFP system register
     * @param value Value to set the VFP system register to
     */
    virtual void SetVFPSystemReg(VFPSystemRegister reg, u32 value) = 0;

    /**
     * Get the current CPSR register
     * @return Returns the value of the CPSR register
     */
    virtual u32 GetCPSR() const = 0;

    /**
     * Set the current CPSR register
     * @param cpsr Value to set CPSR to
     */
    virtual void SetCPSR(u32 cpsr) = 0;

    /**
     * Gets the value stored in a CP15 register.
     * @param reg The CP15 register to retrieve the value from.
     * @return the value stored in the given CP15 register.
     */
    virtual u32 GetCP15Register(CP15Register reg) const = 0;

    /**
     * Stores the given value into the indicated CP15 register.
     * @param reg   The CP15 register to store the value into.
     * @param value The value to store into the CP15 register.
     */
    virtual void SetCP15Register(CP15Register reg, u32 value) = 0;

    /**
     * Creates a CPU context
     * @note The created context may only be used with this instance.
     */
    virtual std::unique_ptr<ThreadContext> NewContext() const = 0;

    /**
     * Saves the current CPU context
     * @param ctx Thread context to save
     */
    virtual void SaveContext(const std::unique_ptr<ThreadContext>& ctx) = 0;

    /**
     * Loads a CPU context
     * @param ctx Thread context to load
     */
    virtual void LoadContext(const std::unique_ptr<ThreadContext>& ctx) = 0;

    /// Prepare core for thread reschedule (if needed to correctly handle state)
    virtual void PrepareReschedule() = 0;

    virtual void PurgeState() = 0;

    std::shared_ptr<Core::Timing::Timer> GetTimer() {
        return timer;
    }

    u32 GetID() const {
        return id;
    }

protected:
    // This us used for serialization. Returning nullptr is valid if page tables are not used.
    virtual std::shared_ptr<Memory::PageTable> GetPageTable() const = 0;

    std::shared_ptr<Core::Timing::Timer> timer;

private:
    u32 id;

    friend class boost::serialization::access;

    template <class Archive>
    void save(Archive& ar, const unsigned int file_version) const {
        const size_t page_table_index = Core::System::GetInstance().Memory().SerializePageTable(GetPageTable());
        ar << page_table_index;

        ar << timer;
        ar << id;
        for (int i = 0; i < 15; i++) {
            const auto r = GetReg(i);
            ar << r;
        }
        const auto pc = GetPC();
        ar << pc;
        const auto cpsr = GetCPSR();
        ar << cpsr;
        int vfp_reg_count = file_version == 0 ? 32 : 64;
        for (int i = 0; i < vfp_reg_count; i++) {
            const auto r = GetVFPReg(i);
            ar << r;
        }
        ar << GetVFPSystemReg(VFP_FPSCR);
        ar << GetVFPSystemReg(VFP_FPEXC);
        ar << GetCP15Register(CP15_THREAD_UPRW);
        ar << GetCP15Register(CP15_THREAD_URO);
    }

    template <class Archive>
    void load(Archive& ar, const unsigned int file_version) {
        PurgeState();

        size_t page_table_index;
        ar >> page_table_index;
        SetPageTable(Core::System::GetInstance().Memory().UnserializePageTable(page_table_index));

        ar >> timer;
        ar >> id;
        u32 r;
        for (int i = 0; i < 15; i++) {
            ar >> r;
            SetReg(i, r);
        }
        ar >> r;
        SetPC(r);
        ar >> r;
        SetCPSR(r);
        int vfp_reg_count = file_version == 0 ? 32 : 64;
        for (int i = 0; i < vfp_reg_count; i++) {
            ar >> r;
            SetVFPReg(i, r);
        }
        ar >> r;
        SetVFPSystemReg(VFP_FPSCR, r);
        ar >> r;
        SetVFPSystemReg(VFP_FPEXC, r);
        ar >> r;
        SetCP15Register(CP15_THREAD_UPRW, r);
        ar >> r;
        SetCP15Register(CP15_THREAD_URO, r);
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()
};

BOOST_CLASS_VERSION(ARM_Interface, 1)
