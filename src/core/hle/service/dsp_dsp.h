// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

#include "core/hle/service/service.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Namespace DSP_DSP

namespace DSP_DSP {

class Interface : public Service::Interface {
public:
    Interface();
    ~Interface() override;

    std::string GetPortName() const override {
        return "dsp::DSP";
    }
};

/// Signal all audio related interrupts.
void SignalAllInterrupts();

/// Signal a specific audio related interrupt based on interrupt id and channel id.
void SignalInterrupt(u32 interrupt, u32 channel);

} // namespace
