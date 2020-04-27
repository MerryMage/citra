// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/archives.h"
#include "core/core.h"
#include "core/hle/kernel/config_mem.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

SERIALIZE_EXPORT_IMPL(ConfigMem::Handler)

namespace boost::serialization {

template <class Archive>
void save_construct_data(Archive& ar, const ConfigMem::Handler* t, const unsigned int) {
    ar << t->GetRef();
}
template void save_construct_data<oarchive>(oarchive& ar, const ConfigMem::Handler* t,
                                            const unsigned int);

template <class Archive>
void load_construct_data(Archive& ar, ConfigMem::Handler* t, const unsigned int) {
    Memory::MemoryRef ref;
    ar >> ref;
    ::new (t) ConfigMem::Handler(Core::System::GetInstance().Memory().GetPointerForRef(ref), ref);
}
template void load_construct_data<iarchive>(iarchive& ar, ConfigMem::Handler* t,
                                            const unsigned int);

} // namespace boost::serialization

namespace ConfigMem {

Handler::Handler(Memory::BackingMemory backing_memory)
    : config_mem(*reinterpret_cast<ConfigMemDef*>(backing_memory.Get())),
      ref(backing_memory.GetRef()) {
    std::memset(&config_mem, 0, sizeof(config_mem));

    // Values extracted from firmware 11.2.0-35E
    config_mem.kernel_version_min = 0x34;
    config_mem.kernel_version_maj = 0x2;
    config_mem.ns_tid = 0x0004013000008002;
    config_mem.sys_core_ver = 0x2;
    config_mem.unit_info = 0x1; // Bit 0 set for Retail
    config_mem.prev_firm = 0x1;
    config_mem.ctr_sdk_ver = 0x0000F297;
    config_mem.firm_version_min = 0x34;
    config_mem.firm_version_maj = 0x2;
    config_mem.firm_sys_core_ver = 0x2;
    config_mem.firm_ctr_sdk_ver = 0x0000F297;
}

Handler::Handler(u8* config_mem, Memory::MemoryRef ref)
    : config_mem(*reinterpret_cast<ConfigMemDef*>(config_mem)), ref(ref) {}

} // namespace ConfigMem
