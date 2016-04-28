// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace AudioCore {

class Sink;

struct SinkDetails {
    SinkDetails() = delete;
    SinkDetails(int id_, const char* const name_, std::function<std::unique_ptr<Sink>()> factory_)
        : id(id_), name(name_), factory(factory_) {}

    const int id;
    const char* const name;
    const std::function<std::unique_ptr<Sink>()> factory;
};

extern const std::vector<SinkDetails> g_sink_details;

} // namespace AudioCore
