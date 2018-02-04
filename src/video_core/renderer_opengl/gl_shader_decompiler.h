// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <functional>
#include "../shader/shader.h"

namespace Pica {
namespace Shader {
namespace Decompiler {

std::string GetCommonDeclarations();

std::string DecompileProgram(const std::array<u32, MAX_PROGRAM_CODE_LENGTH>& program_code,
                             const std::array<u32, MAX_SWIZZLE_DATA_LENGTH>& swizzle_data,
                             u32 main_offset, const std::string& emit_cb = "",
                             const std::string& setemit_cb = "");

} // namespace Decompiler
} // namespace Shader
} // namespace Pica
