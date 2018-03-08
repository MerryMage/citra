// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <atomic>
#include <memory>
#include "video_core/renderer_base.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/swrasterizer/swrasterizer.h"
#include "video_core/video_core.h"

void RendererBase::RefreshRasterizerSetting() {
    VideoCore::Renderer renderer = VideoCore::g_renderer_selection;
    if (rasterizer == nullptr || renderer != rasterizer_active) {
        rasterizer_active = renderer;

        if (renderer == VideoCore::Renderer::OpenGL) {
            rasterizer = std::make_unique<RasterizerOpenGL>();
        } else {
            rasterizer = std::make_unique<VideoCore::SWRasterizer>();
        }
    }
}
