// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>

#include "core/arm/jit/jit_common/mp.h"
#include "common/x64/emitter.h"

namespace ArmJit {
namespace X64 {
namespace InstSelect {

struct RegInfo {
    BitSet32 gpr;
};

class Tile {
public:
    virtual ~Tile() = 0;
    virtual size_t GetNumTemporaries() = 0;
    virtual size_t GetNumArguments() = 0;
    virtual std::vector<RegInfo> GetArgumentLocationConstraints() = 0;
    virtual RegInfo GetRegistersDestroyed() = 0;
    virtual std::vector<Gen::X64Reg> GenerateCode(std::vector<Gen::X64Reg> args, std::vector<Gen::X64Reg> temps) = 0;
};

} // namespace InstSelect
} // namespace X64
} // namespace ArmJit
