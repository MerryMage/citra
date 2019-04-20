// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstddef>
#include <memory>

#include "common/common_types.h"

namespace Common {

class FastmemMapper final {
public:
    /// @param shmem_required Maximum total amount of shared memory that will be `Allocate`-d.
    explicit FastmemMapper(std::size_t shmem_required);
    ~FastmemMapper();

    void* GetBaseAddress() const;
    u8* Allocate(std::size_t size);

    // TODO: Wrap these in a subclass
    u8* AllocRegion();
    void Map(u8* base, VAddr vaddr, u8* backing_memory, std::size_t size);
    void Unmap(u8* base, VAddr vaddr, std::size_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Common
