// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdlib>
#include "common/fastmem_mapper.h"

namespace Common {

BackingMemory::BackingMemory(FastmemMapper* mapper, u8* pointer)
    : mapper(mapper), pointer(pointer) {}

BackingMemory::~BackingMemory() {
    std::free(static_cast<void*>(pointer));
}

FastmemRegion::FastmemRegion() = default;
FastmemRegion::FastmemRegion(FastmemMapper* mapper, u8* pointer)
    : mapper(mapper), pointer(pointer) {}

FastmemRegion::~FastmemRegion() = default;

struct FastmemMapper::Impl {};

FastmemMapper::FastmemMapper(std::size_t shmem_required) : impl(std::make_unique<Impl>()) {}

FastmemMapper::~FastmemMapper() = default;

BackingMemory FastmemMapper::Allocate(std::size_t size) {
    return BackingMemory(this, static_cast<u8*>(std::malloc(size)));
}

FastmemRegion FastmemMapper::AllocateFastmemRegion() {
    return FastmemRegion(this, nullptr);
}

void FastmemMapper::Map(Memory::PageTable&, VAddr vaddr, u8* backing_memory, std::size_t size) {}

void FastmemMapper::Unmap(Memory::PageTable&, VAddr vaddr, std::size_t size) {}

} // namespace Common
