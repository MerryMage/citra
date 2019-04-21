// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#if defined(_WIN32)

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <Windows.h>
#include <memoryapi.h>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/fastmem_mapper.h"
#include "core/memory.h"

namespace Common {

constexpr std::size_t fastmem_region_size = 0x100000000; // 4 GiB
constexpr std::size_t ALLOC_SIZE = 0x10000; // 64 KiB
constexpr std::size_t ALLOC_MASK = ALLOC_SIZE - 1;

namespace {
struct Allocation {
    u8* region_start;
    u8* region_end;
    std::size_t alloc_offset;
};

static BOOL (*UnmapViewOfFile2)(HANDLE Process, PVOID BaseAddress, ULONG UnmapFlags);
static PVOID (*VirtualAlloc2)(HANDLE Process, PVOID BaseAddress, SIZE_T Size, ULONG AllocationType,
                              ULONG PageProtection, MEM_EXTENDED_PARAMETER* ExtendedParameters,
                              ULONG ParameterCount);
static PVOID (*MapViewOfFile3)(HANDLE FileMapping, HANDLE Process, PVOID BaseAddress,
                               ULONG64 Offset, SIZE_T ViewSize, ULONG AllocationType,
                               ULONG PageProtection, MEM_EXTENDED_PARAMETER* ExtendedParameters,
                               ULONG ParameterCount);
static bool fastmem_enabled = false;

struct LoadFunctions {
    LoadFunctions() {
        HMODULE h = ::LoadLibraryW(L"kernelbase.dll");
        ASSERT(h);
        UnmapViewOfFile2 = (decltype(UnmapViewOfFile2))::GetProcAddress(h, "UnmapViewOfFile2");
        VirtualAlloc2 = (decltype(VirtualAlloc2))::GetProcAddress(h, "VirtualAlloc2");
        MapViewOfFile3 = (decltype(MapViewOfFile3))::GetProcAddress(h, "MapViewOfFile3");
        fastmem_enabled = UnmapViewOfFile2 && VirtualAlloc2 && MapViewOfFile3;
        if (UnmapViewOfFile2)
            printf("UnmapViewOfFile2\n");
        if (VirtualAlloc2)
            printf("VirtualAlloc2\n");
        if (MapViewOfFile3)
            printf("MapViewOfFile3\n");

    }
} load_functions;

std::string GetLastErrorAsString() {
    // Get the error message, if any.
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0)
        return std::string(); // No error message has been recorded

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);

    // Free the buffer.
    LocalFree(messageBuffer);

    return message;
}

} // anonymous namespace

struct FastmemMapper::Impl {
    HANDLE shmem;
    std::size_t alloc_offset = 0;
    std::size_t max_alloc = 0;

    std::vector<Allocation> allocations;

    bool IsMappable(Memory::PageTable& page_table, VAddr vaddr);
    bool IsMapped(Memory::PageTable& page_table, VAddr vaddr);
    void DoMap(Memory::PageTable& page_table, VAddr vaddr, std::vector<Allocation>::iterator allocation);
    void DoUnmap(Memory::PageTable& page_table, VAddr vaddr);
};

FastmemMapper::FastmemMapper(std::size_t shmem_required) : impl(std::make_unique<Impl>()) {
    impl->max_alloc = shmem_required;

    impl->shmem = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     static_cast<DWORD>(shmem_required), nullptr);
    if (!impl->shmem) {
        LOG_WARNING(Common_Memory, "Unable to fastmem: CreateFileMappingA failed");
        return;
    }
}

FastmemMapper::~FastmemMapper() {
    if (impl->shmem) {
        CloseHandle(impl->shmem);
    }
}

u8* FastmemMapper::Allocate(std::size_t size) {
    size_t current_offset = impl->alloc_offset;
    impl->alloc_offset += size;
    ASSERT(impl->alloc_offset <= impl->max_alloc);

    u8* region_start = static_cast<u8*>(
        MapViewOfFile(impl->shmem, FILE_MAP_ALL_ACCESS, static_cast<DWORD>(current_offset >> 32),
                      static_cast<DWORD>(current_offset), size));
    ASSERT(region_start);
    impl->allocations.emplace_back(Allocation{region_start, region_start + size, current_offset});
    return region_start;
}

u8* FastmemMapper::AllocRegion() {
    if (!fastmem_enabled) {
        ASSERT_MSG(false, "Fastmem not enabled (Unsupported version of Windows)");
        return nullptr;
    }

    void* base = (*VirtualAlloc2)(GetCurrentProcess(), nullptr, fastmem_region_size, MEM_RESERVE
         | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
    if (!base) {
        ASSERT_MSG(false, "Unable to fastmem: could not mmap fastmem region");
        return nullptr;
    }

    // NOTE: Terminate one ALLOC_SIZE before the end. This is because when splitting a placeholder,
    // Windows requires the remaining size to be nonzero.
    for (size_t i = 0; i < fastmem_region_size - ALLOC_SIZE; i += ALLOC_SIZE) {
        bool success = VirtualFree((u8*)base + i, ALLOC_SIZE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
        if (!success)
            ASSERT_MSG(false, "Could not VirtualFree: {} {}", i, GetLastErrorAsString());
    }

    return static_cast<u8*>(base);
}

bool FastmemMapper::Impl::IsMappable(Memory::PageTable& page_table, VAddr vaddr) {
    const VAddr v = vaddr & ~ALLOC_MASK;
    u8* const base_ptr = page_table.pointers[v >> Memory::PAGE_BITS];

    for (size_t i = 0; i < ALLOC_SIZE; i += Memory::PAGE_SIZE)
        if (page_table.pointers[(v + i) >> Memory::PAGE_BITS] != base_ptr + i)
            return false;

    const auto allocation = std::find_if(allocations.begin(), allocations.end(),
                                        [base_ptr](const auto& x) {
                                            return base_ptr >= x.region_start &&
                                                   base_ptr < x.region_end;
                                        });
    return allocation != allocations.end();
}

bool FastmemMapper::Impl::IsMapped(Memory::PageTable& page_table, VAddr vaddr) {
    const VAddr v = vaddr & ~ALLOC_MASK;
    MEMORY_BASIC_INFORMATION info;
    size_t result = VirtualQuery(page_table.fastmem_base + v, &info, sizeof(info));
    ASSERT(result);
    return info.Type == MEM_MAPPED;
}

void FastmemMapper::Impl::DoMap(Memory::PageTable& page_table, VAddr vaddr, std::vector<Allocation>::iterator allocation) {
    const VAddr v = vaddr & ~ALLOC_MASK;
    u8* const base_ptr = page_table.pointers[v >> Memory::PAGE_BITS];
    const std::size_t offset = base_ptr - allocation->region_start;
    bool success =
        !!(*MapViewOfFile3)(shmem, nullptr, page_table.fastmem_base + v, offset, ALLOC_SIZE,
                          MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
    if (!success)
        ASSERT_MSG(false, "Could not MapViewOfFile3: {}", GetLastErrorAsString());
}

void FastmemMapper::Impl::DoUnmap(Memory::PageTable& page_table, VAddr vaddr) {
    const VAddr v = vaddr & ~ALLOC_MASK;
    bool success = !!(*UnmapViewOfFile2)(GetCurrentProcess(), page_table.fastmem_base + v, MEM_PRESERVE_PLACEHOLDER);
    if (!success)
        ASSERT_MSG(false, "Could not UnmapViewOfFile2: {}", GetLastErrorAsString());
}

void FastmemMapper::Map(Memory::PageTable& page_table, VAddr vaddr, u8* backing_memory, std::size_t size) {
    if (!page_table.fastmem_base) {
        return;
    }

    const auto allocation = std::find_if(impl->allocations.begin(), impl->allocations.end(),
                                        [backing_memory](const auto& x) {
                                            return backing_memory >= x.region_start &&
                                                   backing_memory < x.region_end;
                                        });

    if (allocation == impl->allocations.end()) {
        Unmap(page_table, vaddr, size);
        return;
    }

    for (std::size_t vaddr_offset = 0; vaddr_offset < size; vaddr_offset += ALLOC_SIZE) {
        const VAddr v = vaddr + vaddr_offset;
        const bool mappable = impl->IsMappable(page_table, v);
        const bool mapped = impl->IsMapped(page_table, v);

        if (mapped) {
            impl->DoUnmap(page_table, v);
        }

        if (mappable) {
            impl->DoMap(page_table, v, allocation);
        }
    }
}

void FastmemMapper::Unmap(Memory::PageTable& page_table, VAddr vaddr, std::size_t size) {
    if (!page_table.fastmem_base) {
        return;
    }

    for (std::size_t vaddr_offset = 0; vaddr_offset < size; vaddr_offset += ALLOC_SIZE) {
        const VAddr v = vaddr + vaddr_offset;
        const bool mapped = impl->IsMapped(page_table, v);

        if (mapped) {
            impl->DoUnmap(page_table, v);
        }
    }
}

} // namespace Common

#elif defined(__APPLE__) || defined(__unix__)

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/fastmem_mapper.h"

namespace Common {

constexpr std::size_t fastmem_region_size = 0x100000000; // 4 GiB

namespace {
struct Allocation {
    u8* region_start;
    u8* region_end;
    std::size_t alloc_offset;
};
} // anonymous namespace

struct FastmemMapper::Impl {
    int fd = -1;
    std::size_t alloc_offset = 0;
    std::size_t max_alloc = 0;

    std::vector<Allocation> allocations;
};

FastmemMapper::FastmemMapper(std::size_t shmem_required) : impl(std::make_unique<Impl>()) {
    impl->max_alloc = shmem_required;

    const std::string shm_filename = "/citra." + std::to_string(getpid());
    impl->fd = shm_open(shm_filename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (impl->fd == -1) {
        LOG_WARNING(Common_Memory, "Unable to fastmem: shm_open failed");
        return;
    }
    shm_unlink(shm_filename.c_str());
    if (ftruncate(impl->fd, shmem_required) < 0) {
        LOG_WARNING(Common_Memory, "Unable to fastmem: could not allocate shared memory");
        return;
    }
}

FastmemMapper::~FastmemMapper() {
    if (impl->fd != -1) {
        close(impl->fd);
    }
}

u8* FastmemMapper::Allocate(std::size_t size) {
    size_t current_offset = impl->alloc_offset;
    impl->alloc_offset += size;
    ASSERT(impl->alloc_offset <= impl->max_alloc);

    u8* region_start = static_cast<u8*>(
        mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, impl->fd, current_offset));
    ASSERT(region_start && region_start != MAP_FAILED);
    impl->allocations.emplace_back(Allocation{region_start, region_start + size, current_offset});
    return region_start;
}

u8* FastmemMapper::AllocRegion() {
    void* base = mmap(nullptr, fastmem_region_size, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (base == MAP_FAILED) {
        LOG_WARNING(Common_Memory, "Unable to fastmem: could not mmap fastmem region");
        return nullptr;
    }
    return static_cast<u8*>(base);
}

void FastmemMapper::Map(u8* base, VAddr vaddr, u8* backing_memory, std::size_t size) {
    if (!base) {
        return;
    }

    const auto allocation = std::find_if(impl->allocations.begin(), impl->allocations.end(),
                                        [backing_memory](const auto& x) {
                                            return backing_memory >= x.region_start &&
                                                   backing_memory < x.region_end;
                                        });

    if (allocation == impl->allocations.end()) {
        Unmap(base, vaddr, size);
        return;
    }

    const std::size_t offset = allocation->alloc_offset +
                               static_cast<std::size_t>(backing_memory - allocation->region_start);
    size = std::min(size, static_cast<std::size_t>(allocation->region_end - backing_memory));
    if (!size) {
        return;
    }

    mmap(base + vaddr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, impl->fd, offset);
}

void FastmemMapper::Unmap(u8* base, VAddr vaddr, std::size_t size) {
    if (!base) {
        return;
    }

    mmap(base + vaddr, size, PROT_NONE, MAP_FIXED, -1, 0);
}

} // namespace Common

#else

#include <cstdlib>
#include "common/fastmem_mapper.h"

namespace Common {

struct FastmemMapper::Impl {};

FastmemMapper::FastmemMapper(std::size_t shmem_required) : impl(std::make_unique<Impl>()) {}

FastmemMapper::~FastmemMapper() {}

u8* FastmemMapper::Allocate(std::size_t size) {
    return static_cast<u8*>(std::malloc(size));
}

u8* FastmemMapper::AllocRegion() {
    return nullptr;
}

void FastmemMapper::Map(u8* base, VAddr vaddr, u8* backing_memory, std::size_t size) {}

void FastmemMapper::Unmap(u8* base, VAddr vaddr, std::size_t size) {}

} // namespace Common

#endif
