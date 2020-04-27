// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/version.hpp>
#include "common/common_types.h"

class ThreadContext {
    friend class boost::serialization::access;

    template <class Archive>
    void save(Archive& ar, const unsigned int file_version) const {
        for (std::size_t i = 0; i < 16; i++) {
            const auto r = GetCpuRegister(i);
            ar << r;
        }
        std::size_t fpu_reg_count = file_version == 0 ? 16 : 64;
        for (std::size_t i = 0; i < fpu_reg_count; i++) {
            const auto r = GetFpuRegister(i);
            ar << r;
        }
        const auto r1 = GetCpsr();
        ar << r1;
        const auto r2 = GetFpscr();
        ar << r2;
        const auto r3 = GetFpexc();
        ar << r3;
    }

    template <class Archive>
    void load(Archive& ar, const unsigned int file_version) {
        u32 r;
        for (std::size_t i = 0; i < 16; i++) {
            ar >> r;
            SetCpuRegister(i, r);
        }
        std::size_t fpu_reg_count = file_version == 0 ? 16 : 64;
        for (std::size_t i = 0; i < fpu_reg_count; i++) {
            ar >> r;
            SetFpuRegister(i, r);
        }
        ar >> r;
        SetCpsr(r);
        ar >> r;
        SetFpscr(r);
        ar >> r;
        SetFpexc(r);
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()
public:
    virtual ~ThreadContext() = default;

    virtual void Reset() = 0;
    virtual u32 GetCpuRegister(std::size_t index) const = 0;
    virtual void SetCpuRegister(std::size_t index, u32 value) = 0;
    virtual u32 GetCpsr() const = 0;
    virtual void SetCpsr(u32 value) = 0;
    virtual u32 GetFpuRegister(std::size_t index) const = 0;
    virtual void SetFpuRegister(std::size_t index, u32 value) = 0;
    virtual u32 GetFpscr() const = 0;
    virtual void SetFpscr(u32 value) = 0;
    virtual u32 GetFpexc() const = 0;
    virtual void SetFpexc(u32 value) = 0;

    u32 GetStackPointer() const {
        return GetCpuRegister(13);
    }
    void SetStackPointer(u32 value) {
        return SetCpuRegister(13, value);
    }

    u32 GetLinkRegister() const {
        return GetCpuRegister(14);
    }
    void SetLinkRegister(u32 value) {
        return SetCpuRegister(14, value);
    }

    u32 GetProgramCounter() const {
        return GetCpuRegister(15);
    }
    void SetProgramCounter(u32 value) {
        return SetCpuRegister(15, value);
    }
};

BOOST_CLASS_VERSION(ThreadContext, 1)
