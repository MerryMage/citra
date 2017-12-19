// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fstream>
#include <string>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/string.hpp>
#include "common/logging/log.h"
#include "common/scm_rev.h"
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
    archive(std::string{Common::g_scm_rev});
    Core::System::GetInstance().SerializeState(archive);
}

LoadStateError LoadState(std::ifstream is) {
    StateHeader header = ReadHeader(is);
    if (header.version != SAVE_STATE_VERSION) {
        LOG_ERROR(Core, "Wrong version of save state: version=%d", header.version);
        return LoadStateError::IncorrectVersion;
    }
    std::string save_state_scm_rev;
    cereal::PortableBinaryInputArchive archive(is);
    archive(save_state_scm_rev);
    if (save_state_scm_rev != Common::g_scm_rev) {
        LOG_ERROR(Core, "Save state created on different revision: revision=%s", save_state_scm_rev.c_str());
        return LoadStateError::IncorrectVersion;
    }
    Core::System::GetInstance().SerializeState(archive);
    return LoadStateError::None;
}

} // namespace State
