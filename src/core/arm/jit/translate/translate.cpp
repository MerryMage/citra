// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/arm/jit/ir/micro_ir.h"
#include "core/arm/jit/translate/translate.h"
#include "core/arm/jit/translate/translate_arm.h"

namespace ArmJit {

MicroBlock Translate(const LocationDescriptor& location) {
    if (!location.TFlag) {
        ArmTranslator translator(location);
        return translator.Translate();
    } else {
        // TODO: Implement thumb support
        MicroBlock block(location);
        block.terminal = MicroTerm::Interpret(location);
        return block;
    }
}

} // namespace ArmJit
