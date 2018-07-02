// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstddef>
#include "audio_core/dsp_interface.h"
#include "audio_core/sink.h"
#include "audio_core/sink_details.h"
#include "common/assert.h"

namespace AudioCore {

DspInterface::DspInterface() = default;

DspInterface::~DspInterface() {
}

void DspInterface::SetSink(const std::string& sink_id, const std::string& audio_device) {
    const SinkDetails& sink_details = GetSinkDetails(sink_id);
    sink = sink_details.factory(audio_device);
    resampler.SetOutputSampleRate(sink->GetNativeSampleRate());
    sink->SetCallback(resampler.GetOutputCallback());
}

Sink& DspInterface::GetSink() {
    ASSERT(sink);
    return *sink.get();
}

void DspInterface::EnableStretching(bool enable) {
}

void DspInterface::OutputFrame(const StereoFrame16& frame) {
    if (!sink)
        return;

    resampler.AddSamples(&frame[0][0], frame.size());
}

void DspInterface::FlushResidualStretcherAudio() {
    if (!sink)
        return;
}

} // namespace AudioCore
