// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <atomic>
#include <fstream>
#include "cereal/archives/portable_binary.hpp"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/core_timing.h"
#include "core/save_state.h"

namespace State {

enum SaveStateAction : u8 {
    None,
    Save,
    Load,
};
static std::atomic<SaveStateAction> save_state_action;
static u8 current_slot;
static u64_le current_title_id;

static void WriteHeader(std::ofstream& os) {
    os << current_title_id;
    os << SAVE_STATE_VERSION;
}

static StateHeader ReadHeader(std::ifstream& is) {
    StateHeader header;
    is >> header.title_id;
    is >> header.version;
    return header;
}

template <class Archive>
static void DoState(Archive& archive) {
    // TODO(B3N30): (De)-serialize all necessary states
    // Services

    // Video

    // Audio

    // Kernel

}

static std::string CreateFileName() {
    return FileUtil::GetUserPath(D_USER_IDX) + std::to_string(current_slot) + ".state";
}

void Init() {
    save_state_action = SaveStateAction::None;
    current_slot = 1;
    current_title_id = 0;
}

void ScheduleSave(u8 slot) {
    if (save_state_action == SaveStateAction::None) {
        // No need for a mutex since only UI thread can change the slot
        // if save_state_action == SaveStateAction::None
        current_slot = slot;
        save_state_action = SaveStateAction::Save;
    }
}

bool ShouldSave() {
    return save_state_action == SaveStateAction::Save;
}

void SaveState() {
    std::ofstream os(CreateFileName(), std::ofstream::binary);
    WriteHeader(os);
    cereal::PortableBinaryOutputArchive archive(os);
    DoState(archive);
}

void ScheduleLoad(u8 slot) {
    if (save_state_action == SaveStateAction::None) {
        // No need for a mutex since only UI thread can change the slot
        // if save_state_action == SaveStateAction::None
        current_slot = slot;
        save_state_action = SaveStateAction::Load;
    }
}

bool ShouldLoad() {
    return save_state_action == SaveStateAction::Load;
}

void LoadState() {
    std::ifstream is(CreateFileName(), std::ifstream::binary);
    StateHeader header = ReadHeader(is);
    if (header.version != SAVE_STATE_VERSION) {
        LOG_ERROR(Core, "Wrong version of save state: slot=%d, version=%d", current_slot,
                  header.version);
        return;
    } else if (header.title_id != current_title_id) {
        LOG_ERROR(Core, "Wrong title_id of save state: slot=%d, title_id=%llu", current_slot,
                  header.title_id);
        return;
    }
    cereal::PortableBinaryInputArchive archive(is);
    DoState(archive);
}

void SetTitleID(u64_le id) {
    current_title_id = id;
}

} // namespace State
