// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <vector>

#include "audio_core/null_sink.h"
#include "audio_core/sink_details.h"

#ifdef USE_SDL2
#include "audio_core/sdl2_sink.h"
#endif

namespace AudioCore {

const std::vector<SinkDetails> g_sink_details = {
    { 0, "Null (No Audio)", []() { return std::make_unique<NullSink>(); } },
#ifdef USE_SDL2
    { 1, "SDL2", []() { return std::make_unique<SDL2Sink>(); } },
#endif
};

} // namespace AudioCore
