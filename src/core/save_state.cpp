// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fstream>
#include <cereal/archives/portable_binary.hpp>
#include "common/logging/log.h"
#include "core/core.h"
#include "core/save_state.h"

namespace State {

static void WriteHeader(std::ofstream& os, StateHeader header) {
    os << header.version;
}

static StateHeader ReadHeader(std::ifstream& is) {
    StateHeader header;
    is >> header.version;
    return header;
}

void SaveState(std::ofstream os) {
    WriteHeader(os, {SAVE_STATE_VERSION});
    cereal::PortableBinaryOutputArchive archive(os);
    Core::System::GetInstance().SerializeState(archive);
}

LoadStateError LoadState(std::ifstream is) {
    StateHeader header = ReadHeader(is);
    if (header.version != SAVE_STATE_VERSION) {
        LOG_ERROR(Core, "Wrong version of save state: version=%d", header.version);
        return LoadStateError::IncorrectVersion;
    }

    cereal::PortableBinaryInputArchive archive(is);
    Core::System::GetInstance().SerializeState(archive);
    return LoadStateError::None;
}

} // namespace State
