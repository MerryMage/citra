// Copyright 2020 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iterator>
#include <list>
#include <memory>
#include <boost/serialization/binary_object.hpp>
#include "common/archives.h"
#include "common/assert.h"
#include "core/backing_memory_manager.h"

SERIALIZE_EXPORT_IMPL(Memory::BackingMemoryManager)

namespace Memory {

namespace {

struct Allocation {
    bool is_free;
    std::size_t offset;
    std::size_t size;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar& is_free;
        ar& offset;
        ar& size;
    }
};

} // anonymous namespace

struct BackingMemoryManager::Impl final {
    u8* memory;
    std::size_t max_alloc;
    std::list<Allocation> allocations;
};

BackingMemory::BackingMemory(std::shared_ptr<BackingMemoryManager> manager_, u8* pointer_, MemoryRef ref_, size_t size_) : manager(manager_), pointer(pointer_), ref(ref_), size(size_) {}
BackingMemory::~BackingMemory() = default;

FastmemRegion::FastmemRegion() : manager(nullptr), pointer(nullptr) {}
FastmemRegion::FastmemRegion(std::shared_ptr<BackingMemoryManager> manager_, u8* pointer_) : manager(manager_), pointer(pointer_) {}
FastmemRegion::~FastmemRegion() = default;

BackingMemoryManager::BackingMemoryManager(std::size_t total_required) : impl(std::make_unique<Impl>()) {
    impl->memory = static_cast<u8*>(std::malloc(total_required));
    impl->max_alloc = total_required;
    impl->allocations.emplace_back(Allocation{true, 0, total_required});
}

BackingMemoryManager::~BackingMemoryManager() {
    std::free(static_cast<void*>(impl->memory));
}

BackingMemory BackingMemoryManager::AllocateBackingMemory(std::size_t size) {
    const auto iter = std::find_if(impl->allocations.begin(), impl->allocations.end(), [size](const auto& allocation) { return allocation.is_free && allocation.size >= size; });
    ASSERT_MSG(iter != impl->allocations.end(), "Out of memory when allcoating {} bytes", size);

    if (iter->size == size) {
        iter->is_free = false;
        return BackingMemory{shared_from_this(), impl->memory + iter->offset, static_cast<MemoryRef>(iter->offset), size};
    }

    const std::size_t offset = iter->offset;

    iter->offset += size;
    iter->size -= size;

    impl->allocations.insert(iter, Allocation{false, offset, size});

    return BackingMemory{shared_from_this(), impl->memory + offset, static_cast<MemoryRef>(offset), size};
}

void BackingMemoryManager::FreeBackingMemory(MemoryRef offset) {
    auto iter = std::find_if(impl->allocations.begin(), impl->allocations.end(), [offset](const auto& allocation) { return !allocation.is_free && MemoryRef{allocation.offset} == offset; });
    ASSERT_MSG(iter != impl->allocations.end(), "Could not find backing memory to free");

    iter->is_free = true;

    // Coalesce free space

    if (iter != impl->allocations.begin()) {
        auto prev_iter = std::prev(iter);
        ASSERT(prev_iter->offset + prev_iter->size == iter->offset);
        if (prev_iter->is_free) {
            prev_iter->size += iter->size;
            impl->allocations.erase(iter);
            iter = prev_iter;
        }
    }

    auto next_iter = std::next(iter);
    if (next_iter != impl->allocations.end()) {
        ASSERT(iter->offset + iter->size == next_iter->offset);
        if (next_iter->is_free) {
            iter->size += next_iter->size;
            impl->allocations.erase(next_iter);
        }
    }
}

u8* BackingMemoryManager::GetPointerForRef(MemoryRef ref) {
    return impl->memory + ref;
}

MemoryRef BackingMemoryManager::GetRefForPointer(u8* pointer) {
    return MemoryRef{pointer - impl->memory};
}

FastmemRegion BackingMemoryManager::AllocateFastmemRegion() {
    return {};
}

void BackingMemoryManager::Map(Memory::PageTable&, VAddr, u8* in, std::size_t) {
    const std::ptrdiff_t offset = in - impl->memory;
    ASSERT(0 <= offset && offset < static_cast<std::ptrdiff_t>(impl->max_alloc));
}

void BackingMemoryManager::Unmap(Memory::PageTable&, VAddr, std::size_t) {}

void BackingMemoryManager::Serialize(std::array<std::ptrdiff_t, PAGE_TABLE_NUM_ENTRIES>& out, const std::array<u8*, PAGE_TABLE_NUM_ENTRIES>& in) {
    for (size_t i = 0; i < PAGE_TABLE_NUM_ENTRIES; ++i) {
        if (in[i] == nullptr) {
            out[i] = -1;
        } else {
            const std::ptrdiff_t offset = in[i] - impl->memory;
            ASSERT(0 <= offset && offset < static_cast<std::ptrdiff_t>(impl->max_alloc));
            out[i] = offset;
        }
    }
}

void BackingMemoryManager::Unserialize(std::array<u8*, PAGE_TABLE_NUM_ENTRIES>& out, const std::array<std::ptrdiff_t, PAGE_TABLE_NUM_ENTRIES>& in) {
    for (size_t i = 0; i < PAGE_TABLE_NUM_ENTRIES; ++i) {
        if (in[i] == -1) {
            out[i] = nullptr;
        } else {
            const std::ptrdiff_t offset = in[i];
            ASSERT(0 <= offset && offset < static_cast<std::ptrdiff_t>(impl->max_alloc));
            out[i] = impl->memory + offset;
        }
    }
}

BackingMemoryManager::BackingMemoryManager() : impl(std::make_unique<Impl>()) {}

template <class Archive>
void BackingMemoryManager::save(Archive& ar, const unsigned int file_version) const {
    ar << impl->max_alloc;

    const size_t count = impl->allocations.size();
    ar << count;
    for (const auto& allocation : impl->allocations) {
        ar << allocation;
        ar << boost::serialization::make_binary_object(impl->memory + allocation.offset, allocation.size);
    }
}

template <class Archive>
void BackingMemoryManager::load(Archive& ar, const unsigned int file_version) {
    ar >> impl->max_alloc;

    if (!impl->memory) {
        impl->memory = static_cast<u8*>(std::malloc(impl->max_alloc));
    }

    impl->allocations.clear();
    size_t count;
    ar >> count;
    for (size_t i = 0; i < count; ++i) {
        Allocation allocation;
        ar >> allocation;
        ar >> boost::serialization::make_binary_object(impl->memory + allocation.offset, allocation.size);
        impl->allocations.push_back(allocation);
    }
}

SERIALIZE_IMPL(BackingMemoryManager)

} // namespace Memory
