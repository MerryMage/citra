// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>

#include <boost/variant.hpp>
#include <catch.hpp>

#include "common/common_types.h"
#include "common/scope_exit.h"

#include "core/arm/jit/ir/micro_ir.h"
#include "core/arm/jit/translate/translate.h"
#include "core/arm/jit/jit_interpret/jit_interpret.h"
#include "core/arm/skyeye_common/armstate.h"
#include "core/core.h"
#include "core/memory_setup.h"

TEST_CASE("Test ARM Translator", "[arm-translator]") {
    using namespace ArmJit;

    // We're just setting up a block of memory for us to use to write
    // instructions to, as ArmJit::ArmTranslator reads memory directly.

    std::array<u32, 1024> memory;
    Core::Init();
    SCOPE_EXIT({ Core::Shutdown(); });
    Memory::MapMemoryRegion(0, 1024 * sizeof(u32), reinterpret_cast<u8*>(memory.data()));
    SCOPE_EXIT({ Memory::UnmapRegion(0, 1024 * sizeof(u32)); });

    SECTION("adds r1, r2, #3") {
        // Write test program to memory
        memory[0] = 0xE2921003; // adds r1, r2, #3
        memory[1] = 0xEAFFFFFE; // b +#0

        // Translate it to our IR.
        MicroBlock block = Translate({0, false, false});

        // Verify IR
        REQUIRE(block.instructions.size() == 4);
        REQUIRE(block.location == LocationDescriptor(0, false, false));  // block at { pc: 0x0, T: false, E: false }:
        auto iter = block.instructions.begin();
        REQUIRE((**iter).GetOp() == MicroOp::GetGPR);                    //   u32 %0 = GetGPR R2
        ++iter;
        REQUIRE((**iter).GetOp() == MicroOp::ConstU32);                  //   u32 %1 = ConstU32 0x3
        ++iter;
        REQUIRE((**iter).GetOp() == MicroOp::Add);                       //   u32 %2 = Add[NZCV] %0, %1
        REQUIRE((**iter).ReadFlags() == MicroArmFlags::None);
        REQUIRE((**iter).WriteFlags() == MicroArmFlags::NZCV);
        ++iter;
        REQUIRE((**iter).GetOp() == MicroOp::SetGPR);                    //   SetGPR R3, %2
        ++iter;
        REQUIRE(iter == block.instructions.end());
        REQUIRE(block.terminal.type() == typeid(MicroTerm::LinkBlock));  //   LinkBlock { pc: 0x4, T: false, E: false }
        REQUIRE(boost::get<MicroTerm::LinkBlock>(block.terminal).next == LocationDescriptor(4, false, false));

        Interpret::ARM_MicroInterpreter interpreter(PrivilegeMode::USER32MODE);
        for (int i = 0; i < 15; i++)
            interpreter.SetReg(i, i);

        interpreter.ExecuteInstructions(2);

        REQUIRE(interpreter.GetReg(0) == 0);
        REQUIRE(interpreter.GetReg(1) == 5);
        REQUIRE(interpreter.GetReg(2) == 2);
        REQUIRE(interpreter.GetReg(3) == 3);
        REQUIRE(interpreter.GetReg(15) == 4);
    }
}
