// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// The purpose of this header is to provide
// a single place to change should any change
// in seralization backend be made.
//
// Ideally there shouldn't be any reference
// to the cereal seralization library outside
// of this file.

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#define INSTANTIATE_SERALIZATION_FUNCTION(FN_NAME)                                                 \
    template void FN_NAME<cereal::PortableBinaryInputArchive>(cereal::PortableBinaryInputArchive & \
                                                              archive);                            \
    template void FN_NAME<cereal::PortableBinaryOutputArchive>(                                    \
        cereal::PortableBinaryOutputArchive & archive);
