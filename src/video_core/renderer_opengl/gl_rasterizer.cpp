// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <glad/glad.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "common/vector_math.h"
#include "core/hw/gpu.h"
#include "core/settings.h"
#include "video_core/pica_state.h"
#include "video_core/regs_framebuffer.h"
#include "video_core/regs_rasterizer.h"
#include "video_core/regs_texturing.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/pica_to_gl.h"
#include "video_core/renderer_opengl/renderer_opengl.h"

using PixelFormat = SurfaceParams::PixelFormat;
using SurfaceType = SurfaceParams::SurfaceType;

MICROPROFILE_DEFINE(OpenGL_VAO, "OpenGL", "Vertex Array Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_VS, "OpenGL", "Vertex Shader Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_GS, "OpenGL", "Geometry Shader Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Drawing, "OpenGL", "Drawing", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Blits, "OpenGL", "Blits", MP_RGB(100, 100, 255));
MICROPROFILE_DEFINE(OpenGL_CacheManagement, "OpenGL", "Cache Mgmt", MP_RGB(100, 255, 100));

enum class UniformBindings : GLuint { Common, VS, GS };

static void SetShaderUniformBlockBinding(GLuint shader, const char* name, UniformBindings binding,
                                         size_t expected_size) {
    GLuint ub_index = glGetUniformBlockIndex(shader, name);
    if (ub_index != GL_INVALID_INDEX) {
        GLint ub_size = 0;
        glGetActiveUniformBlockiv(shader, ub_index, GL_UNIFORM_BLOCK_DATA_SIZE, &ub_size);
        ASSERT_MSG(ub_size == expected_size,
                   "Uniform block size did not match! Got %d, expected %zu",
                   static_cast<int>(ub_size), expected_size);
        glUniformBlockBinding(shader, ub_index, static_cast<GLuint>(binding));
    }
}

static void SetShaderUniformBlockBindings(GLuint shader) {
    SetShaderUniformBlockBinding(shader, "shader_data", UniformBindings::Common,
                                 sizeof(RasterizerOpenGL::UniformData));
    SetShaderUniformBlockBinding(shader, "vs_config", UniformBindings::VS,
                                 sizeof(RasterizerOpenGL::VSUniformData));
    SetShaderUniformBlockBinding(shader, "gs_config", UniformBindings::GS,
                                 sizeof(RasterizerOpenGL::GSUniformData));
}

void RasterizerOpenGL::PicaUniformsData::SetFromRegs(const Pica::ShaderRegs& regs,
                                                     const Pica::Shader::ShaderSetup& setup) {
    for (size_t it = 0; it < 16; ++it) {
        bools[it].b = setup.uniforms.b[it] ? GL_TRUE : GL_FALSE;
    }
    for (size_t it = 0; it < 4; ++it) {
        i[it][0] = regs.int_uniforms[it].x;
        i[it][1] = regs.int_uniforms[it].y;
        i[it][2] = regs.int_uniforms[it].z;
        i[it][3] = regs.int_uniforms[it].w;
    }
    std::memcpy(&f[0], &setup.uniforms.f[0], sizeof(f));
}

RasterizerOpenGL::RasterizerOpenGL() {
    shader_dirty = true;

    has_ARB_separate_shader_objects = false;
    has_ARB_vertex_attrib_binding = false;
    has_ARB_buffer_storage = false;

    GLint ext_num;
    glGetIntegerv(GL_NUM_EXTENSIONS, &ext_num);
    for (GLint i = 0; i < ext_num; i++) {
        std::string extension{reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i))};

        if (extension == "GL_ARB_separate_shader_objects") {
            has_ARB_separate_shader_objects = true;
        } else if (extension == "GL_ARB_vertex_attrib_binding") {
            has_ARB_vertex_attrib_binding = true;
        } else if (extension == "GL_ARB_buffer_storage") {
            has_ARB_buffer_storage = true;
        }
    }

    uniform_buffer_offset_alignment = 1;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniform_buffer_offset_alignment);

    // Clipping plane 0 is always enabled for PICA fixed clip plane z <= 0
    state.clip_distance[0] = true;

    // Create sampler objects
    for (size_t i = 0; i < texture_samplers.size(); ++i) {
        texture_samplers[i].Create();
        state.texture_units[i].sampler = texture_samplers[i].sampler.handle;
    }

    // Generate VBO, VAO and UBO
    vertex_buffer.Create();
    sw_vao.Create();
    uniform_buffer.Create();

    state.draw.vertex_array = sw_vao.handle;
    state.draw.vertex_buffer = vertex_buffer.handle;
    state.Apply();

    uniform_block_data.dirty = true;

    uniform_block_data.lut_dirty.fill(true);

    uniform_block_data.fog_lut_dirty = true;

    uniform_block_data.proctex_noise_lut_dirty = true;
    uniform_block_data.proctex_color_map_dirty = true;
    uniform_block_data.proctex_alpha_map_dirty = true;
    uniform_block_data.proctex_lut_dirty = true;
    uniform_block_data.proctex_diff_lut_dirty = true;

    // Set vertex attributes
    glVertexAttribPointer(GLShader::ATTRIBUTE_POSITION, 4, GL_FLOAT, GL_FALSE,
                          sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, position));
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_POSITION);

    glVertexAttribPointer(GLShader::ATTRIBUTE_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, color));
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_COLOR);

    glVertexAttribPointer(GLShader::ATTRIBUTE_TEXCOORD0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, tex_coord0));
    glVertexAttribPointer(GLShader::ATTRIBUTE_TEXCOORD1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, tex_coord1));
    glVertexAttribPointer(GLShader::ATTRIBUTE_TEXCOORD2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, tex_coord2));
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_TEXCOORD0);
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_TEXCOORD1);
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_TEXCOORD2);

    glVertexAttribPointer(GLShader::ATTRIBUTE_TEXCOORD0_W, 1, GL_FLOAT, GL_FALSE,
                          sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, tex_coord0_w));
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_TEXCOORD0_W);

    glVertexAttribPointer(GLShader::ATTRIBUTE_NORMQUAT, 4, GL_FLOAT, GL_FALSE,
                          sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, normquat));
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_NORMQUAT);

    glVertexAttribPointer(GLShader::ATTRIBUTE_VIEW, 3, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, view));
    glEnableVertexAttribArray(GLShader::ATTRIBUTE_VIEW);

    // Create render framebuffer
    framebuffer.Create();

    // Allocate and bind lighting lut textures
    lighting_lut.Create();
    state.lighting_lut.texture_buffer = lighting_lut.handle;
    state.Apply();
    lighting_lut_buffer.Create();
    glBindBuffer(GL_TEXTURE_BUFFER, lighting_lut_buffer.handle);
    glBufferData(GL_TEXTURE_BUFFER,
                 sizeof(GLfloat) * 2 * 256 * Pica::LightingRegs::NumLightingSampler, nullptr,
                 GL_STATIC_DRAW);
    glActiveTexture(TextureUnits::LightingLUT.Enum());
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, lighting_lut_buffer.handle);

    // Setup the LUT for the fog
    fog_lut.Create();
    state.fog_lut.texture_buffer = fog_lut.handle;
    state.Apply();
    fog_lut_buffer.Create();
    glBindBuffer(GL_TEXTURE_BUFFER, fog_lut_buffer.handle);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(GLfloat) * 2 * 128, nullptr, GL_STATIC_DRAW);
    glActiveTexture(TextureUnits::FogLUT.Enum());
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, fog_lut_buffer.handle);

    // Setup the noise LUT for proctex
    proctex_noise_lut.Create();
    state.proctex_noise_lut.texture_buffer = proctex_noise_lut.handle;
    state.Apply();
    proctex_noise_lut_buffer.Create();
    glBindBuffer(GL_TEXTURE_BUFFER, proctex_noise_lut_buffer.handle);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(GLfloat) * 2 * 128, nullptr, GL_STATIC_DRAW);
    glActiveTexture(TextureUnits::ProcTexNoiseLUT.Enum());
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, proctex_noise_lut_buffer.handle);

    // Setup the color map for proctex
    proctex_color_map.Create();
    state.proctex_color_map.texture_buffer = proctex_color_map.handle;
    state.Apply();
    proctex_color_map_buffer.Create();
    glBindBuffer(GL_TEXTURE_BUFFER, proctex_color_map_buffer.handle);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(GLfloat) * 2 * 128, nullptr, GL_STATIC_DRAW);
    glActiveTexture(TextureUnits::ProcTexColorMap.Enum());
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, proctex_color_map_buffer.handle);

    // Setup the alpha map for proctex
    proctex_alpha_map.Create();
    state.proctex_alpha_map.texture_buffer = proctex_alpha_map.handle;
    state.Apply();
    proctex_alpha_map_buffer.Create();
    glBindBuffer(GL_TEXTURE_BUFFER, proctex_alpha_map_buffer.handle);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(GLfloat) * 2 * 128, nullptr, GL_STATIC_DRAW);
    glActiveTexture(TextureUnits::ProcTexAlphaMap.Enum());
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, proctex_alpha_map_buffer.handle);

    // Setup the LUT for proctex
    proctex_lut.Create();
    state.proctex_lut.texture_buffer = proctex_lut.handle;
    state.Apply();
    proctex_lut_buffer.Create();
    glBindBuffer(GL_TEXTURE_BUFFER, proctex_lut_buffer.handle);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(GLfloat) * 4 * 256, nullptr, GL_STATIC_DRAW);
    glActiveTexture(TextureUnits::ProcTexLUT.Enum());
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, proctex_lut_buffer.handle);

    // Setup the difference LUT for proctex
    proctex_diff_lut.Create();
    state.proctex_diff_lut.texture_buffer = proctex_diff_lut.handle;
    state.Apply();
    proctex_diff_lut_buffer.Create();
    glBindBuffer(GL_TEXTURE_BUFFER, proctex_diff_lut_buffer.handle);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(GLfloat) * 4 * 256, nullptr, GL_STATIC_DRAW);
    glActiveTexture(TextureUnits::ProcTexDiffLUT.Enum());
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, proctex_diff_lut_buffer.handle);

    if (has_ARB_separate_shader_objects) {
        hw_vao.Create();
        hw_vao_enabled_attributes.fill(false);

        stream_buffer = OGLStreamBuffer::MakeBuffer(has_ARB_buffer_storage, GL_ARRAY_BUFFER);
        stream_buffer->Create(STREAM_BUFFER_SIZE, STREAM_BUFFER_SIZE / 2);
        state.draw.vertex_buffer = stream_buffer->GetHandle();

        pipeline.Create();
        vs_input_index_min = 0;
        vs_input_index_max = 0;
        state.draw.program_pipeline = pipeline.handle;
        state.draw.shader_program = 0;
        state.draw.vertex_array = hw_vao.handle;
        state.Apply();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, stream_buffer->GetHandle());

        vs_default_shader.Create(GLShader::GenerateDefaultVertexShader(true).c_str(), nullptr,
                                 nullptr, {}, true);
        SetShaderUniformBlockBindings(vs_default_shader.handle);
    }

    accelerate_draw = AccelDraw::Disabled;

    glEnable(GL_BLEND);

    // Sync fixed function OpenGL state
    SyncClipEnabled();
    SyncClipCoef();
    SyncCullMode();
    SyncBlendEnabled();
    SyncBlendFuncs();
    SyncBlendColor();
    SyncLogicOp();
    SyncStencilTest();
    SyncDepthTest();
    SyncColorWriteMask();
    SyncStencilWriteMask();
    SyncDepthWriteMask();
}

RasterizerOpenGL::~RasterizerOpenGL() {
    if (stream_buffer != nullptr) {
        state.draw.vertex_buffer = stream_buffer->GetHandle();
        state.Apply();
        stream_buffer->Release();
    }
}

/**
 * This is a helper function to resolve an issue when interpolating opposite quaternions. See below
 * for a detailed description of this issue (yuriks):
 *
 * For any rotation, there are two quaternions Q, and -Q, that represent the same rotation. If you
 * interpolate two quaternions that are opposite, instead of going from one rotation to another
 * using the shortest path, you'll go around the longest path. You can test if two quaternions are
 * opposite by checking if Dot(Q1, Q2) < 0. In that case, you can flip either of them, therefore
 * making Dot(Q1, -Q2) positive.
 *
 * This solution corrects this issue per-vertex before passing the quaternions to OpenGL. This is
 * correct for most cases but can still rotate around the long way sometimes. An implementation
 * which did `lerp(lerp(Q1, Q2), Q3)` (with proper weighting), applying the dot product check
 * between each step would work for those cases at the cost of being more complex to implement.
 *
 * Fortunately however, the 3DS hardware happens to also use this exact same logic to work around
 * these issues, making this basic implementation actually more accurate to the hardware.
 */
static bool AreQuaternionsOpposite(Math::Vec4<Pica::float24> qa, Math::Vec4<Pica::float24> qb) {
    Math::Vec4f a{qa.x.ToFloat32(), qa.y.ToFloat32(), qa.z.ToFloat32(), qa.w.ToFloat32()};
    Math::Vec4f b{qb.x.ToFloat32(), qb.y.ToFloat32(), qb.z.ToFloat32(), qb.w.ToFloat32()};

    return (Math::Dot(a, b) < 0.f);
}

void RasterizerOpenGL::AddTriangle(const Pica::Shader::OutputVertex& v0,
                                   const Pica::Shader::OutputVertex& v1,
                                   const Pica::Shader::OutputVertex& v2) {
    vertex_batch.emplace_back(v0, false);
    vertex_batch.emplace_back(v1, AreQuaternionsOpposite(v0.quat, v1.quat));
    vertex_batch.emplace_back(v2, AreQuaternionsOpposite(v0.quat, v2.quat));
}

static constexpr std::array<GLenum, 4> vs_attrib_types{
    GL_BYTE,          // VertexAttributeFormat::BYTE
    GL_UNSIGNED_BYTE, // VertexAttributeFormat::UBYTE
    GL_SHORT,         // VertexAttributeFormat::SHORT
    GL_FLOAT          // VertexAttributeFormat::FLOAT
};

void RasterizerOpenGL::AnalyzeVertexArray(bool is_indexed) {
    const auto& regs = Pica::g_state.regs;
    const auto& vertex_attributes = regs.pipeline.vertex_attributes;

    // Load input attributes
    const u32 base_address = vertex_attributes.GetPhysicalBaseAddress();

    const auto& index_info = regs.pipeline.index_array;
    const u8* index_address_8 = Memory::GetPhysicalPointer(base_address + index_info.offset);
    const u16* index_address_16 = reinterpret_cast<const u16*>(index_address_8);
    const bool index_u16 = index_info.format != 0;

    u32 vertex_min = regs.pipeline.vertex_offset;
    u32 vertex_max = regs.pipeline.vertex_offset + regs.pipeline.num_vertices - 1;
    if (is_indexed) {
        vertex_min = 0xFFFF;
        vertex_max = 0;
        for (u32 index = 0; index < regs.pipeline.num_vertices; ++index) {
            u32 vertex = index_u16 ? index_address_16[index] : index_address_8[index];
            if (index_u16 && vertex == 0xFFFF) {
                continue;
            }
            vertex_min = std::min(vertex_min, vertex);
            vertex_max = std::max(vertex_max, vertex);
        }
    }
    const u32 vertex_num = vertex_max - vertex_min + 1;
    vs_input_index_min = static_cast<GLint>(vertex_min);
    vs_input_index_max = static_cast<GLint>(vertex_max);

    vs_input_size = 0;
    for (auto& loader : vertex_attributes.attribute_loaders) {
        if (loader.component_count != 0) {
            vs_input_size += loader.byte_count * vertex_num;
        }
    }
}

void RasterizerOpenGL::SetupVertexArray(u8* array_ptr, GLintptr buffer_offset) {
    MICROPROFILE_SCOPE(OpenGL_VAO);
    const auto& regs = Pica::g_state.regs;
    const auto& vertex_attributes = regs.pipeline.vertex_attributes;

    // Load input attributes
    const u32 base_address = vertex_attributes.GetPhysicalBaseAddress();

    state.draw.vertex_array = hw_vao.handle;
    state.draw.vertex_buffer = stream_buffer->GetHandle();
    state.Apply();

    std::array<bool, 16> enable_attributes{};
    ASSERT(vertex_attributes.GetNumTotalAttributes() < 16);

    for (int i = 0; i < 12; ++i) {
        const auto& loader = vertex_attributes.attribute_loaders[i];

        if (!loader.component_count || !loader.byte_count) {
            continue;
        }

        u32 offset = 0;
        for (unsigned comp = 0; comp < loader.component_count && comp < 12; ++comp) {
            int attribute_index = loader.GetComponent(comp);
            if (attribute_index < 12) {
                if (vertex_attributes.GetNumElements(attribute_index) != 0) {
                    offset = Common::AlignUp(
                        offset, vertex_attributes.GetElementSizeInBytes(attribute_index));

                    u32 input_reg =
                        regs.vs.GetRegisterForAttribute(static_cast<u32>(attribute_index));
                    glVertexAttribPointer(input_reg,
                                          vertex_attributes.GetNumElements(attribute_index),
                                          vs_attrib_types[static_cast<size_t>(
                                              vertex_attributes.GetFormat(attribute_index))],
                                          GL_FALSE, static_cast<GLsizei>(loader.byte_count),
                                          reinterpret_cast<GLvoid*>(buffer_offset + offset));
                    enable_attributes[input_reg] = true;

                    offset += vertex_attributes.GetStride(attribute_index);
                }
            } else {
                // Attribute ids 12, 13, 14 and 15 signify 4, 8, 12 and 16-byte paddings,
                // respectively
                offset = Common::AlignUp(offset, 4);
                offset += (attribute_index - 11) * 4;
            }
        }

        PAddr data_addr = vertex_attributes.GetPhysicalBaseAddress() + loader.data_offset +
                          (vs_input_index_min * loader.byte_count);

        const u32 vertex_num = vs_input_index_max - vs_input_index_min + 1;
        u32 data_size = loader.byte_count * vertex_num;

        // TODO: cache this too
        res_cache.FlushRegion(data_addr, data_size, nullptr);
        std::memcpy(array_ptr, Memory::GetPhysicalPointer(data_addr), data_size);

        array_ptr += data_size;
        buffer_offset += data_size;
    }

    for (u32 i = 0; i < 16; ++i) {
        if (enable_attributes[i] != hw_vao_enabled_attributes[i]) {
            if (enable_attributes[i]) {
                glEnableVertexAttribArray(i);
            } else {
                glDisableVertexAttribArray(i);
            }
            hw_vao_enabled_attributes[i] = enable_attributes[i];
        }

        if (vertex_attributes.IsDefaultAttribute(i)) {
            u32 reg = regs.vs.GetRegisterForAttribute(i);
            if (!enable_attributes[reg]) {
                glVertexAttrib4f(reg, Pica::g_state.input_default_attributes.attr[i].x.ToFloat32(),
                                 Pica::g_state.input_default_attributes.attr[i].y.ToFloat32(),
                                 Pica::g_state.input_default_attributes.attr[i].z.ToFloat32(),
                                 Pica::g_state.input_default_attributes.attr[i].w.ToFloat32());
            }
        }
    }
}

void RasterizerOpenGL::SetupVertexShader(VSUniformData* ub_ptr, GLintptr buffer_offset) {
    MICROPROFILE_SCOPE(OpenGL_VS);

    ub_ptr->uniforms.SetFromRegs(Pica::g_state.regs.vs, Pica::g_state.vs);
    glBindBufferRange(GL_UNIFORM_BUFFER, 1, stream_buffer->GetHandle(), buffer_offset,
                      sizeof(VSUniformData));

    GLuint shader;
    const GLShader::PicaVSConfig vs_config(Pica::g_state.regs, Pica::g_state.vs);

    auto map_it = vs_shader_map.find(vs_config);
    if (map_it == vs_shader_map.end()) {
        std::string vs_program = GLShader::GenerateVertexShader(Pica::g_state.vs, vs_config);

        VertexShader& cached_shader = vs_shader_cache[vs_program];
        if (cached_shader.shader.handle == 0) {
            cached_shader.shader.handle =
                GLShader::LoadProgram(vs_program.c_str(), nullptr, nullptr, {}, true);
            SetShaderUniformBlockBindings(cached_shader.shader.handle);
        }
        vs_shader_map[vs_config] = &cached_shader;
        shader = cached_shader.shader.handle;
    } else {
        shader = map_it->second->shader.handle;
    }

    glUseProgramStages(pipeline.handle, GL_VERTEX_SHADER_BIT, shader);
}

void RasterizerOpenGL::SetupGeometryShader(GSUniformData* ub_ptr, GLintptr buffer_offset) {
    MICROPROFILE_SCOPE(OpenGL_GS);
    const auto& regs = Pica::g_state.regs;

    GLuint shader;

    if (regs.pipeline.use_gs == Pica::PipelineRegs::UseGS::No) {
        const GLShader::PicaGSConfigCommon gs_config(regs);
        GeometryShader& cached_shader = gs_default_shaders[gs_config];
        if (cached_shader.shader.handle == 0) {
            cached_shader.shader.handle = GLShader::LoadProgram(
                nullptr, GLShader::GenerateDefaultGeometryShader(gs_config).c_str(), nullptr, {},
                true);
            SetShaderUniformBlockBindings(cached_shader.shader.handle);
        }
        shader = cached_shader.shader.handle;
    } else {
        ub_ptr->uniforms.SetFromRegs(Pica::g_state.regs.gs, Pica::g_state.gs);
        glBindBufferRange(GL_UNIFORM_BUFFER, 2, stream_buffer->GetHandle(), buffer_offset,
                          sizeof(GSUniformData));

        // The uniform b15 is set to true after every geometry shader invocation.
        Pica::g_state.gs.uniforms.b[15] = true;

        const GLShader::PicaGSConfig gs_config(regs, Pica::g_state.gs);

        auto map_it = gs_shader_map.find(gs_config);
        if (map_it == gs_shader_map.end()) {
            std::string gs_program = GLShader::GenerateGeometryShader(Pica::g_state.gs, gs_config);

            GeometryShader& cached_shader = gs_shader_cache[gs_program];
            if (cached_shader.shader.handle == 0) {
                cached_shader.shader.handle =
                    GLShader::LoadProgram(nullptr, gs_program.c_str(), nullptr, {}, true);
                SetShaderUniformBlockBindings(cached_shader.shader.handle);
            }
            gs_shader_map[gs_config] = &cached_shader;
            shader = cached_shader.shader.handle;
        } else {
            shader = map_it->second->shader.handle;
        }
    }

    glUseProgramStages(pipeline.handle, GL_GEOMETRY_SHADER_BIT, shader);
}

bool RasterizerOpenGL::AccelerateDrawBatch(bool is_indexed) {
    if (!has_ARB_separate_shader_objects) {
        LOG_CRITICAL(Render_OpenGL,
                     "GL_ARB_separate_shader_objects extension unsupported, disabling HW shaders");
        Settings::values.hw_shaders = Settings::HwShaders::Off;
        return false;
    }

    const auto& regs = Pica::g_state.regs;
    if (regs.pipeline.use_gs != Pica::PipelineRegs::UseGS::No) {
        if (regs.pipeline.gs_config.mode != Pica::PipelineRegs::GSMode::Point) {
            return false;
        }
        if ((regs.gs.max_input_attribute_index + 1) %
                (regs.pipeline.vs_outmap_total_minus_1_a + 1) !=
            0) {
            return false;
        }
        switch ((regs.gs.max_input_attribute_index + 1) /
                (regs.pipeline.vs_outmap_total_minus_1_a + 1)) {
        case 1: // GL_POINTS
        case 2: // GL_LINES
        case 4: // GL_LINES_ADJACENCY
        case 3: // GL_TRIANGLES
        case 6: // GL_TRIANGLES_ADJACENCY
            break;
        default:
            return false;
        }
    }

    accelerate_draw = is_indexed ? AccelDraw::Indexed : AccelDraw::Arrays;
    DrawTriangles();

    return true;
}

void RasterizerOpenGL::DrawTriangles() {
    if (vertex_batch.empty() && accelerate_draw == AccelDraw::Disabled)
        return;

    MICROPROFILE_SCOPE(OpenGL_Drawing);
    const auto& regs = Pica::g_state.regs;

    const bool has_stencil =
        regs.framebuffer.framebuffer.depth_format == Pica::FramebufferRegs::DepthFormat::D24S8;

    const bool write_color_fb =
        state.color_mask.red_enabled == GL_TRUE || state.color_mask.green_enabled == GL_TRUE ||
        state.color_mask.blue_enabled == GL_TRUE || state.color_mask.alpha_enabled == GL_TRUE;

    const bool write_depth_fb =
        (state.depth.test_enabled && state.depth.write_mask == GL_TRUE) ||
        (has_stencil && state.stencil.test_enabled && state.stencil.write_mask != 0);

    const bool using_color_fb =
        regs.framebuffer.framebuffer.GetColorBufferPhysicalAddress() != 0 && write_color_fb;
    const bool using_depth_fb =
        regs.framebuffer.framebuffer.GetDepthBufferPhysicalAddress() != 0 &&
        (write_depth_fb || regs.framebuffer.output_merger.depth_test_enable != 0 ||
         (has_stencil && state.stencil.test_enabled));

    MathUtil::Rectangle<s32> viewport_rect_unscaled{
        // These registers hold half-width and half-height, so must be multiplied by 2
        regs.rasterizer.viewport_corner.x,  // left
        regs.rasterizer.viewport_corner.y + // top
            static_cast<s32>(Pica::float24::FromRaw(regs.rasterizer.viewport_size_y).ToFloat32() *
                             2),
        regs.rasterizer.viewport_corner.x + // right
            static_cast<s32>(Pica::float24::FromRaw(regs.rasterizer.viewport_size_x).ToFloat32() *
                             2),
        regs.rasterizer.viewport_corner.y // bottom
    };

    Surface color_surface;
    Surface depth_surface;
    MathUtil::Rectangle<u32> surfaces_rect;
    std::tie(color_surface, depth_surface, surfaces_rect) =
        res_cache.GetFramebufferSurfaces(using_color_fb, using_depth_fb, viewport_rect_unscaled);

    const u16 res_scale = color_surface != nullptr
                              ? color_surface->res_scale
                              : (depth_surface == nullptr ? 1u : depth_surface->res_scale);

    MathUtil::Rectangle<u32> draw_rect{
        static_cast<u32>(MathUtil::Clamp<s32>(static_cast<s32>(surfaces_rect.left) +
                                                  viewport_rect_unscaled.left * res_scale,
                                              surfaces_rect.left, surfaces_rect.right)), // Left
        static_cast<u32>(MathUtil::Clamp<s32>(static_cast<s32>(surfaces_rect.bottom) +
                                                  viewport_rect_unscaled.top * res_scale,
                                              surfaces_rect.bottom, surfaces_rect.top)), // Top
        static_cast<u32>(MathUtil::Clamp<s32>(static_cast<s32>(surfaces_rect.left) +
                                                  viewport_rect_unscaled.right * res_scale,
                                              surfaces_rect.left, surfaces_rect.right)), // Right
        static_cast<u32>(MathUtil::Clamp<s32>(static_cast<s32>(surfaces_rect.bottom) +
                                                  viewport_rect_unscaled.bottom * res_scale,
                                              surfaces_rect.bottom, surfaces_rect.top))}; // Bottom

    // Bind the framebuffer surfaces
    state.draw.draw_framebuffer = framebuffer.handle;
    state.Apply();

    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_surface != nullptr ? color_surface->texture.handle : 0, 0);
    if (depth_surface != nullptr) {
        if (has_stencil) {
            // attach both depth and stencil
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   depth_surface->texture.handle, 0);
        } else {
            // attach depth
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   depth_surface->texture.handle, 0);
            // clear stencil attachment
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }
    } else {
        // clear both depth and stencil attachment
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);
    }

    // Sync the viewport
    state.viewport.x =
        static_cast<GLint>(surfaces_rect.left) + viewport_rect_unscaled.left * res_scale;
    state.viewport.y =
        static_cast<GLint>(surfaces_rect.bottom) + viewport_rect_unscaled.bottom * res_scale;
    state.viewport.width = static_cast<GLsizei>(viewport_rect_unscaled.GetWidth() * res_scale);
    state.viewport.height = static_cast<GLsizei>(viewport_rect_unscaled.GetHeight() * res_scale);

    if (uniform_block_data.data.framebuffer_scale != res_scale) {
        uniform_block_data.data.framebuffer_scale = res_scale;
        uniform_block_data.dirty = true;
    }

    // Scissor checks are window-, not viewport-relative, which means that if the cached texture
    // sub-rect changes, the scissor bounds also need to be updated.
    GLint scissor_x1 =
        static_cast<GLint>(surfaces_rect.left + regs.rasterizer.scissor_test.x1 * res_scale);
    GLint scissor_y1 =
        static_cast<GLint>(surfaces_rect.bottom + regs.rasterizer.scissor_test.y1 * res_scale);
    // x2, y2 have +1 added to cover the entire pixel area, otherwise you might get cracks when
    // scaling or doing multisampling.
    GLint scissor_x2 =
        static_cast<GLint>(surfaces_rect.left + (regs.rasterizer.scissor_test.x2 + 1) * res_scale);
    GLint scissor_y2 = static_cast<GLint>(surfaces_rect.bottom +
                                          (regs.rasterizer.scissor_test.y2 + 1) * res_scale);

    if (uniform_block_data.data.scissor_x1 != scissor_x1 ||
        uniform_block_data.data.scissor_x2 != scissor_x2 ||
        uniform_block_data.data.scissor_y1 != scissor_y1 ||
        uniform_block_data.data.scissor_y2 != scissor_y2) {

        uniform_block_data.data.scissor_x1 = scissor_x1;
        uniform_block_data.data.scissor_x2 = scissor_x2;
        uniform_block_data.data.scissor_y1 = scissor_y1;
        uniform_block_data.data.scissor_y2 = scissor_y2;
        uniform_block_data.dirty = true;
    }

    // Sync and bind the texture surfaces
    const auto pica_textures = regs.texturing.GetTextures();
    for (unsigned texture_index = 0; texture_index < pica_textures.size(); ++texture_index) {
        const auto& texture = pica_textures[texture_index];

        if (texture.enabled) {
            texture_samplers[texture_index].SyncWithConfig(texture.config);
            Surface surface = res_cache.GetTextureSurface(texture);
            if (surface != nullptr) {
                state.texture_units[texture_index].texture_2d = surface->texture.handle;
            } else {
                // Can occur when texture addr is null or its memory is unmapped/invalid
                state.texture_units[texture_index].texture_2d = 0;
            }
        } else {
            state.texture_units[texture_index].texture_2d = 0;
        }
    }

    // Sync and bind the shader
    if (shader_dirty) {
        SetShader();
        shader_dirty = false;
    }

    // Sync the lighting luts
    for (unsigned index = 0; index < uniform_block_data.lut_dirty.size(); index++) {
        if (uniform_block_data.lut_dirty[index]) {
            SyncLightingLUT(index);
            uniform_block_data.lut_dirty[index] = false;
        }
    }

    // Sync the fog lut
    if (uniform_block_data.fog_lut_dirty) {
        SyncFogLUT();
        uniform_block_data.fog_lut_dirty = false;
    }

    // Sync the proctex noise lut
    if (uniform_block_data.proctex_noise_lut_dirty) {
        SyncProcTexNoiseLUT();
        uniform_block_data.proctex_noise_lut_dirty = false;
    }

    // Sync the proctex color map
    if (uniform_block_data.proctex_color_map_dirty) {
        SyncProcTexColorMap();
        uniform_block_data.proctex_color_map_dirty = false;
    }

    // Sync the proctex alpha map
    if (uniform_block_data.proctex_alpha_map_dirty) {
        SyncProcTexAlphaMap();
        uniform_block_data.proctex_alpha_map_dirty = false;
    }

    // Sync the proctex lut
    if (uniform_block_data.proctex_lut_dirty) {
        SyncProcTexLUT();
        uniform_block_data.proctex_lut_dirty = false;
    }

    // Sync the proctex difference lut
    if (uniform_block_data.proctex_diff_lut_dirty) {
        SyncProcTexDiffLUT();
        uniform_block_data.proctex_diff_lut_dirty = false;
    }

    // Sync the uniform data
    if (uniform_block_data.dirty) {
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniform_buffer.handle);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(UniformData), &uniform_block_data.data,
                     GL_STATIC_DRAW);
        uniform_block_data.dirty = false;
    }

    // Viewport can have negative offsets or larger
    // dimensions than our framebuffer sub-rect.
    // Enable scissor test to prevent drawing
    // outside of the framebuffer region
    state.scissor.enabled = true;
    state.scissor.x = draw_rect.left;
    state.scissor.y = draw_rect.bottom;
    state.scissor.width = draw_rect.GetWidth();
    state.scissor.height = draw_rect.GetHeight();
    state.Apply();

    // Draw the vertex batch
    if (accelerate_draw != AccelDraw::Disabled) {
        GLenum primitive_mode;
        switch (regs.pipeline.triangle_topology) {
        case Pica::PipelineRegs::TriangleTopology::Shader:
        case Pica::PipelineRegs::TriangleTopology::List:
            primitive_mode = GL_TRIANGLES;
            break;
        case Pica::PipelineRegs::TriangleTopology::Fan:
            primitive_mode = GL_TRIANGLE_FAN;
            break;
        case Pica::PipelineRegs::TriangleTopology::Strip:
            primitive_mode = GL_TRIANGLE_STRIP;
            break;
        default:
            UNREACHABLE();
        }

        const bool use_gs = regs.pipeline.use_gs == Pica::PipelineRegs::UseGS::Yes;
        if (use_gs) {
            switch ((regs.gs.max_input_attribute_index + 1) /
                    (regs.pipeline.vs_outmap_total_minus_1_a + 1)) {
            case 1:
                primitive_mode = GL_POINTS;
                break;
            case 2:
                primitive_mode = GL_LINES;
                break;
            case 4:
                primitive_mode = GL_LINES_ADJACENCY;
                break;
            case 3:
                primitive_mode = GL_TRIANGLES;
                break;
            case 6:
                primitive_mode = GL_TRIANGLES_ADJACENCY;
                break;
            default:
                UNREACHABLE();
            }
        }

        const bool is_indexed = accelerate_draw == AccelDraw::Indexed;
        const bool index_u16 = regs.pipeline.index_array.format != 0;
        const size_t index_buffer_size = regs.pipeline.num_vertices * (index_u16 ? 2 : 1);

        AnalyzeVertexArray(is_indexed);
        state.draw.vertex_buffer = stream_buffer->GetHandle();
        state.Apply();

        size_t buffer_size = static_cast<size_t>(vs_input_size);
        if (is_indexed) {
            buffer_size = Common::AlignUp(buffer_size, 4) + index_buffer_size;
        }
        buffer_size =
            Common::AlignUp(buffer_size, uniform_buffer_offset_alignment) + sizeof(VSUniformData);
        if (use_gs) {
            buffer_size = Common::AlignUp(buffer_size, uniform_buffer_offset_alignment) +
                          sizeof(GSUniformData);
        }

        size_t ptr_pos = 0;
        u8* buffer_ptr;
        GLintptr buffer_offset;
        std::tie(buffer_ptr, buffer_offset) = stream_buffer->Map(
            static_cast<GLsizeiptr>(buffer_size), std::max(4, uniform_buffer_offset_alignment));

        SetupVertexArray(buffer_ptr, buffer_offset);
        ptr_pos += vs_input_size;

        GLintptr index_buffer_offset = 0;
        if (is_indexed) {
            ptr_pos = Common::AlignUp(ptr_pos, 4);

            const u8* index_data = Memory::GetPhysicalPointer(
                regs.pipeline.vertex_attributes.GetPhysicalBaseAddress() +
                regs.pipeline.index_array.offset);

            std::memcpy(&buffer_ptr[ptr_pos], index_data, index_buffer_size);
            index_buffer_offset = buffer_offset + static_cast<GLintptr>(ptr_pos);

            ptr_pos += index_buffer_size;
        }

        ptr_pos = Common::AlignUp(ptr_pos, uniform_buffer_offset_alignment);
        SetupVertexShader(reinterpret_cast<VSUniformData*>(&buffer_ptr[ptr_pos]),
                          buffer_offset + static_cast<GLintptr>(ptr_pos));
        ptr_pos += sizeof(VSUniformData);

        ptr_pos = Common::AlignUp(ptr_pos, uniform_buffer_offset_alignment);
        SetupGeometryShader(use_gs ? reinterpret_cast<GSUniformData*>(&buffer_ptr[ptr_pos])
                                   : nullptr,
                            buffer_offset + static_cast<GLintptr>(ptr_pos));

        stream_buffer->Unmap();

        glUseProgramStages(pipeline.handle, GL_FRAGMENT_SHADER_BIT, current_shader->shader.handle);

        if (is_indexed) {
            glDrawRangeElementsBaseVertex(
                primitive_mode, vs_input_index_min, vs_input_index_max, regs.pipeline.num_vertices,
                index_u16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE,
                reinterpret_cast<const void*>(index_buffer_offset), -vs_input_index_min);
        } else {
            glDrawArrays(primitive_mode, 0, regs.pipeline.num_vertices);
        }
    } else {
        state.draw.vertex_array = sw_vao.handle;
        state.draw.vertex_buffer = vertex_buffer.handle;
        if (has_ARB_separate_shader_objects) {
            glUseProgramStages(pipeline.handle, GL_VERTEX_SHADER_BIT, vs_default_shader.handle);
            glUseProgramStages(pipeline.handle, GL_GEOMETRY_SHADER_BIT, 0);
            glUseProgramStages(pipeline.handle, GL_FRAGMENT_SHADER_BIT,
                               current_shader->shader.handle);
        } else {
            state.draw.shader_program = current_shader->shader.handle;
        }
        state.Apply();

        glBufferData(GL_ARRAY_BUFFER, vertex_batch.size() * sizeof(HardwareVertex),
                     vertex_batch.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_batch.size()));
    }

    // Disable scissor test
    state.scissor.enabled = false;

    vertex_batch.clear();
    accelerate_draw = AccelDraw::Disabled;

    // Unbind textures for potential future use as framebuffer attachments
    for (unsigned texture_index = 0; texture_index < pica_textures.size(); ++texture_index) {
        state.texture_units[texture_index].texture_2d = 0;
    }
    state.Apply();

    // Mark framebuffer surfaces as dirty
    MathUtil::Rectangle<u32> draw_rect_unscaled{
        draw_rect.left / res_scale, draw_rect.top / res_scale, draw_rect.right / res_scale,
        draw_rect.bottom / res_scale};

    if (color_surface != nullptr && write_color_fb) {
        auto interval = color_surface->GetSubRectInterval(draw_rect_unscaled);
        res_cache.InvalidateRegion(boost::icl::first(interval), boost::icl::length(interval),
                                   color_surface);
    }
    if (depth_surface != nullptr && write_depth_fb) {
        auto interval = depth_surface->GetSubRectInterval(draw_rect_unscaled);
        res_cache.InvalidateRegion(boost::icl::first(interval), boost::icl::length(interval),
                                   depth_surface);
    }
}

void RasterizerOpenGL::NotifyPicaRegisterChanged(u32 id) {
    const auto& regs = Pica::g_state.regs;

    switch (id) {
    // Culling
    case PICA_REG_INDEX(rasterizer.cull_mode):
        SyncCullMode();
        break;

    // Clipping plane
    case PICA_REG_INDEX(rasterizer.clip_enable):
        SyncClipEnabled();
        break;

    case PICA_REG_INDEX_WORKAROUND(rasterizer.clip_coef[0], 0x48):
    case PICA_REG_INDEX_WORKAROUND(rasterizer.clip_coef[1], 0x49):
    case PICA_REG_INDEX_WORKAROUND(rasterizer.clip_coef[2], 0x4a):
    case PICA_REG_INDEX_WORKAROUND(rasterizer.clip_coef[3], 0x4b):
        SyncClipCoef();
        break;

    // Depth modifiers
    case PICA_REG_INDEX(rasterizer.viewport_depth_range):
        SyncDepthScale();
        break;
    case PICA_REG_INDEX(rasterizer.viewport_depth_near_plane):
        SyncDepthOffset();
        break;

    // Depth buffering
    case PICA_REG_INDEX(rasterizer.depthmap_enable):
        shader_dirty = true;
        break;

    // Blending
    case PICA_REG_INDEX(framebuffer.output_merger.alphablend_enable):
        SyncBlendEnabled();
        break;
    case PICA_REG_INDEX(framebuffer.output_merger.alpha_blending):
        SyncBlendFuncs();
        break;
    case PICA_REG_INDEX(framebuffer.output_merger.blend_const):
        SyncBlendColor();
        break;

    // Fog state
    case PICA_REG_INDEX(texturing.fog_color):
        SyncFogColor();
        break;
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[0], 0xe8):
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[1], 0xe9):
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[2], 0xea):
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[3], 0xeb):
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[4], 0xec):
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[5], 0xed):
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[6], 0xee):
    case PICA_REG_INDEX_WORKAROUND(texturing.fog_lut_data[7], 0xef):
        uniform_block_data.fog_lut_dirty = true;
        break;

    // ProcTex state
    case PICA_REG_INDEX(texturing.proctex):
    case PICA_REG_INDEX(texturing.proctex_lut):
    case PICA_REG_INDEX(texturing.proctex_lut_offset):
        shader_dirty = true;
        break;

    case PICA_REG_INDEX(texturing.proctex_noise_u):
    case PICA_REG_INDEX(texturing.proctex_noise_v):
    case PICA_REG_INDEX(texturing.proctex_noise_frequency):
        SyncProcTexNoise();
        break;

    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[0], 0xb0):
    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[1], 0xb1):
    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[2], 0xb2):
    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[3], 0xb3):
    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[4], 0xb4):
    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[5], 0xb5):
    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[6], 0xb6):
    case PICA_REG_INDEX_WORKAROUND(texturing.proctex_lut_data[7], 0xb7):
        using Pica::TexturingRegs;
        switch (regs.texturing.proctex_lut_config.ref_table.Value()) {
        case TexturingRegs::ProcTexLutTable::Noise:
            uniform_block_data.proctex_noise_lut_dirty = true;
            break;
        case TexturingRegs::ProcTexLutTable::ColorMap:
            uniform_block_data.proctex_color_map_dirty = true;
            break;
        case TexturingRegs::ProcTexLutTable::AlphaMap:
            uniform_block_data.proctex_alpha_map_dirty = true;
            break;
        case TexturingRegs::ProcTexLutTable::Color:
            uniform_block_data.proctex_lut_dirty = true;
            break;
        case TexturingRegs::ProcTexLutTable::ColorDiff:
            uniform_block_data.proctex_diff_lut_dirty = true;
            break;
        }
        break;

    // Alpha test
    case PICA_REG_INDEX(framebuffer.output_merger.alpha_test):
        SyncAlphaTest();
        shader_dirty = true;
        break;

    // Sync GL stencil test + stencil write mask
    // (Pica stencil test function register also contains a stencil write mask)
    case PICA_REG_INDEX(framebuffer.output_merger.stencil_test.raw_func):
        SyncStencilTest();
        SyncStencilWriteMask();
        break;
    case PICA_REG_INDEX(framebuffer.output_merger.stencil_test.raw_op):
    case PICA_REG_INDEX(framebuffer.framebuffer.depth_format):
        SyncStencilTest();
        break;

    // Sync GL depth test + depth and color write mask
    // (Pica depth test function register also contains a depth and color write mask)
    case PICA_REG_INDEX(framebuffer.output_merger.depth_test_enable):
        SyncDepthTest();
        SyncDepthWriteMask();
        SyncColorWriteMask();
        break;

    // Sync GL depth and stencil write mask
    // (This is a dedicated combined depth / stencil write-enable register)
    case PICA_REG_INDEX(framebuffer.framebuffer.allow_depth_stencil_write):
        SyncDepthWriteMask();
        SyncStencilWriteMask();
        break;

    // Sync GL color write mask
    // (This is a dedicated color write-enable register)
    case PICA_REG_INDEX(framebuffer.framebuffer.allow_color_write):
        SyncColorWriteMask();
        break;

    // Scissor test
    case PICA_REG_INDEX(rasterizer.scissor_test.mode):
        shader_dirty = true;
        break;

    // Logic op
    case PICA_REG_INDEX(framebuffer.output_merger.logic_op):
        SyncLogicOp();
        break;

    case PICA_REG_INDEX(texturing.main_config):
        shader_dirty = true;
        break;

    // Texture 0 type
    case PICA_REG_INDEX(texturing.texture0.type):
        shader_dirty = true;
        break;

    // TEV stages
    // (This also syncs fog_mode and fog_flip which are part of tev_combiner_buffer_input)
    case PICA_REG_INDEX(texturing.tev_stage0.color_source1):
    case PICA_REG_INDEX(texturing.tev_stage0.color_modifier1):
    case PICA_REG_INDEX(texturing.tev_stage0.color_op):
    case PICA_REG_INDEX(texturing.tev_stage0.color_scale):
    case PICA_REG_INDEX(texturing.tev_stage1.color_source1):
    case PICA_REG_INDEX(texturing.tev_stage1.color_modifier1):
    case PICA_REG_INDEX(texturing.tev_stage1.color_op):
    case PICA_REG_INDEX(texturing.tev_stage1.color_scale):
    case PICA_REG_INDEX(texturing.tev_stage2.color_source1):
    case PICA_REG_INDEX(texturing.tev_stage2.color_modifier1):
    case PICA_REG_INDEX(texturing.tev_stage2.color_op):
    case PICA_REG_INDEX(texturing.tev_stage2.color_scale):
    case PICA_REG_INDEX(texturing.tev_stage3.color_source1):
    case PICA_REG_INDEX(texturing.tev_stage3.color_modifier1):
    case PICA_REG_INDEX(texturing.tev_stage3.color_op):
    case PICA_REG_INDEX(texturing.tev_stage3.color_scale):
    case PICA_REG_INDEX(texturing.tev_stage4.color_source1):
    case PICA_REG_INDEX(texturing.tev_stage4.color_modifier1):
    case PICA_REG_INDEX(texturing.tev_stage4.color_op):
    case PICA_REG_INDEX(texturing.tev_stage4.color_scale):
    case PICA_REG_INDEX(texturing.tev_stage5.color_source1):
    case PICA_REG_INDEX(texturing.tev_stage5.color_modifier1):
    case PICA_REG_INDEX(texturing.tev_stage5.color_op):
    case PICA_REG_INDEX(texturing.tev_stage5.color_scale):
    case PICA_REG_INDEX(texturing.tev_combiner_buffer_input):
        shader_dirty = true;
        break;
    case PICA_REG_INDEX(texturing.tev_stage0.const_r):
        SyncTevConstColor(0, regs.texturing.tev_stage0);
        break;
    case PICA_REG_INDEX(texturing.tev_stage1.const_r):
        SyncTevConstColor(1, regs.texturing.tev_stage1);
        break;
    case PICA_REG_INDEX(texturing.tev_stage2.const_r):
        SyncTevConstColor(2, regs.texturing.tev_stage2);
        break;
    case PICA_REG_INDEX(texturing.tev_stage3.const_r):
        SyncTevConstColor(3, regs.texturing.tev_stage3);
        break;
    case PICA_REG_INDEX(texturing.tev_stage4.const_r):
        SyncTevConstColor(4, regs.texturing.tev_stage4);
        break;
    case PICA_REG_INDEX(texturing.tev_stage5.const_r):
        SyncTevConstColor(5, regs.texturing.tev_stage5);
        break;

    // TEV combiner buffer color
    case PICA_REG_INDEX(texturing.tev_combiner_buffer_color):
        SyncCombinerColor();
        break;

    // Fragment lighting switches
    case PICA_REG_INDEX(lighting.disable):
    case PICA_REG_INDEX(lighting.max_light_index):
    case PICA_REG_INDEX(lighting.config0):
    case PICA_REG_INDEX(lighting.config1):
    case PICA_REG_INDEX(lighting.abs_lut_input):
    case PICA_REG_INDEX(lighting.lut_input):
    case PICA_REG_INDEX(lighting.lut_scale):
    case PICA_REG_INDEX(lighting.light_enable):
        break;

    // Fragment lighting specular 0 color
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].specular_0, 0x140 + 0 * 0x10):
        SyncLightSpecular0(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].specular_0, 0x140 + 1 * 0x10):
        SyncLightSpecular0(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].specular_0, 0x140 + 2 * 0x10):
        SyncLightSpecular0(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].specular_0, 0x140 + 3 * 0x10):
        SyncLightSpecular0(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].specular_0, 0x140 + 4 * 0x10):
        SyncLightSpecular0(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].specular_0, 0x140 + 5 * 0x10):
        SyncLightSpecular0(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].specular_0, 0x140 + 6 * 0x10):
        SyncLightSpecular0(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].specular_0, 0x140 + 7 * 0x10):
        SyncLightSpecular0(7);
        break;

    // Fragment lighting specular 1 color
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].specular_1, 0x141 + 0 * 0x10):
        SyncLightSpecular1(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].specular_1, 0x141 + 1 * 0x10):
        SyncLightSpecular1(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].specular_1, 0x141 + 2 * 0x10):
        SyncLightSpecular1(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].specular_1, 0x141 + 3 * 0x10):
        SyncLightSpecular1(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].specular_1, 0x141 + 4 * 0x10):
        SyncLightSpecular1(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].specular_1, 0x141 + 5 * 0x10):
        SyncLightSpecular1(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].specular_1, 0x141 + 6 * 0x10):
        SyncLightSpecular1(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].specular_1, 0x141 + 7 * 0x10):
        SyncLightSpecular1(7);
        break;

    // Fragment lighting diffuse color
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].diffuse, 0x142 + 0 * 0x10):
        SyncLightDiffuse(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].diffuse, 0x142 + 1 * 0x10):
        SyncLightDiffuse(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].diffuse, 0x142 + 2 * 0x10):
        SyncLightDiffuse(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].diffuse, 0x142 + 3 * 0x10):
        SyncLightDiffuse(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].diffuse, 0x142 + 4 * 0x10):
        SyncLightDiffuse(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].diffuse, 0x142 + 5 * 0x10):
        SyncLightDiffuse(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].diffuse, 0x142 + 6 * 0x10):
        SyncLightDiffuse(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].diffuse, 0x142 + 7 * 0x10):
        SyncLightDiffuse(7);
        break;

    // Fragment lighting ambient color
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].ambient, 0x143 + 0 * 0x10):
        SyncLightAmbient(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].ambient, 0x143 + 1 * 0x10):
        SyncLightAmbient(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].ambient, 0x143 + 2 * 0x10):
        SyncLightAmbient(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].ambient, 0x143 + 3 * 0x10):
        SyncLightAmbient(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].ambient, 0x143 + 4 * 0x10):
        SyncLightAmbient(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].ambient, 0x143 + 5 * 0x10):
        SyncLightAmbient(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].ambient, 0x143 + 6 * 0x10):
        SyncLightAmbient(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].ambient, 0x143 + 7 * 0x10):
        SyncLightAmbient(7);
        break;

    // Fragment lighting position
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].x, 0x144 + 0 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].z, 0x145 + 0 * 0x10):
        SyncLightPosition(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].x, 0x144 + 1 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].z, 0x145 + 1 * 0x10):
        SyncLightPosition(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].x, 0x144 + 2 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].z, 0x145 + 2 * 0x10):
        SyncLightPosition(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].x, 0x144 + 3 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].z, 0x145 + 3 * 0x10):
        SyncLightPosition(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].x, 0x144 + 4 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].z, 0x145 + 4 * 0x10):
        SyncLightPosition(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].x, 0x144 + 5 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].z, 0x145 + 5 * 0x10):
        SyncLightPosition(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].x, 0x144 + 6 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].z, 0x145 + 6 * 0x10):
        SyncLightPosition(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].x, 0x144 + 7 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].z, 0x145 + 7 * 0x10):
        SyncLightPosition(7);
        break;

    // Fragment spot lighting direction
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].spot_x, 0x146 + 0 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].spot_z, 0x147 + 0 * 0x10):
        SyncLightSpotDirection(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].spot_x, 0x146 + 1 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].spot_z, 0x147 + 1 * 0x10):
        SyncLightSpotDirection(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].spot_x, 0x146 + 2 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].spot_z, 0x147 + 2 * 0x10):
        SyncLightSpotDirection(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].spot_x, 0x146 + 3 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].spot_z, 0x147 + 3 * 0x10):
        SyncLightSpotDirection(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].spot_x, 0x146 + 4 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].spot_z, 0x147 + 4 * 0x10):
        SyncLightSpotDirection(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].spot_x, 0x146 + 5 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].spot_z, 0x147 + 5 * 0x10):
        SyncLightSpotDirection(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].spot_x, 0x146 + 6 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].spot_z, 0x147 + 6 * 0x10):
        SyncLightSpotDirection(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].spot_x, 0x146 + 7 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].spot_z, 0x147 + 7 * 0x10):
        SyncLightSpotDirection(7);
        break;

    // Fragment lighting light source config
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].config, 0x149 + 0 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].config, 0x149 + 1 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].config, 0x149 + 2 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].config, 0x149 + 3 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].config, 0x149 + 4 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].config, 0x149 + 5 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].config, 0x149 + 6 * 0x10):
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].config, 0x149 + 7 * 0x10):
        shader_dirty = true;
        break;

    // Fragment lighting distance attenuation bias
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].dist_atten_bias, 0x014A + 0 * 0x10):
        SyncLightDistanceAttenuationBias(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].dist_atten_bias, 0x014A + 1 * 0x10):
        SyncLightDistanceAttenuationBias(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].dist_atten_bias, 0x014A + 2 * 0x10):
        SyncLightDistanceAttenuationBias(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].dist_atten_bias, 0x014A + 3 * 0x10):
        SyncLightDistanceAttenuationBias(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].dist_atten_bias, 0x014A + 4 * 0x10):
        SyncLightDistanceAttenuationBias(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].dist_atten_bias, 0x014A + 5 * 0x10):
        SyncLightDistanceAttenuationBias(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].dist_atten_bias, 0x014A + 6 * 0x10):
        SyncLightDistanceAttenuationBias(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].dist_atten_bias, 0x014A + 7 * 0x10):
        SyncLightDistanceAttenuationBias(7);
        break;

    // Fragment lighting distance attenuation scale
    case PICA_REG_INDEX_WORKAROUND(lighting.light[0].dist_atten_scale, 0x014B + 0 * 0x10):
        SyncLightDistanceAttenuationScale(0);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[1].dist_atten_scale, 0x014B + 1 * 0x10):
        SyncLightDistanceAttenuationScale(1);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[2].dist_atten_scale, 0x014B + 2 * 0x10):
        SyncLightDistanceAttenuationScale(2);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[3].dist_atten_scale, 0x014B + 3 * 0x10):
        SyncLightDistanceAttenuationScale(3);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[4].dist_atten_scale, 0x014B + 4 * 0x10):
        SyncLightDistanceAttenuationScale(4);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[5].dist_atten_scale, 0x014B + 5 * 0x10):
        SyncLightDistanceAttenuationScale(5);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[6].dist_atten_scale, 0x014B + 6 * 0x10):
        SyncLightDistanceAttenuationScale(6);
        break;
    case PICA_REG_INDEX_WORKAROUND(lighting.light[7].dist_atten_scale, 0x014B + 7 * 0x10):
        SyncLightDistanceAttenuationScale(7);
        break;

    // Fragment lighting global ambient color (emission + ambient * ambient)
    case PICA_REG_INDEX_WORKAROUND(lighting.global_ambient, 0x1c0):
        SyncGlobalAmbient();
        break;

    // Fragment lighting lookup tables
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[0], 0x1c8):
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[1], 0x1c9):
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[2], 0x1ca):
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[3], 0x1cb):
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[4], 0x1cc):
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[5], 0x1cd):
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[6], 0x1ce):
    case PICA_REG_INDEX_WORKAROUND(lighting.lut_data[7], 0x1cf): {
        auto& lut_config = regs.lighting.lut_config;
        uniform_block_data.lut_dirty[lut_config.type] = true;
        break;
    }
    }
}

void RasterizerOpenGL::FlushAll() {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.FlushAll();
}

void RasterizerOpenGL::FlushRegion(PAddr addr, u32 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.FlushRegion(addr, size);
}

void RasterizerOpenGL::InvalidateRegion(PAddr addr, u32 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.InvalidateRegion(addr, size, nullptr);
}

void RasterizerOpenGL::FlushAndInvalidateRegion(PAddr addr, u32 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.FlushRegion(addr, size);
    res_cache.InvalidateRegion(addr, size, nullptr);
}

bool RasterizerOpenGL::AccelerateDisplayTransfer(const GPU::Regs::DisplayTransferConfig& config) {
    MICROPROFILE_SCOPE(OpenGL_Blits);

    SurfaceParams src_params;
    src_params.addr = config.GetPhysicalInputAddress();
    src_params.width = config.output_width;
    src_params.stride = config.input_width;
    src_params.height = config.output_height;
    src_params.is_tiled = !config.input_linear;
    src_params.pixel_format = SurfaceParams::PixelFormatFromGPUPixelFormat(config.input_format);
    src_params.UpdateParams();

    SurfaceParams dst_params;
    dst_params.addr = config.GetPhysicalOutputAddress();
    dst_params.width = config.scaling != config.NoScale ? config.output_width.Value() / 2
                                                        : config.output_width.Value();
    dst_params.height = config.scaling == config.ScaleXY ? config.output_height.Value() / 2
                                                         : config.output_height.Value();
    dst_params.is_tiled = config.input_linear != config.dont_swizzle;
    dst_params.pixel_format = SurfaceParams::PixelFormatFromGPUPixelFormat(config.output_format);
    dst_params.UpdateParams();

    MathUtil::Rectangle<u32> src_rect;
    Surface src_surface;
    std::tie(src_surface, src_rect) =
        res_cache.GetSurfaceSubRect(src_params, ScaleMatch::Ignore, true);
    if (src_surface == nullptr)
        return false;

    dst_params.res_scale = src_surface->res_scale;

    MathUtil::Rectangle<u32> dst_rect;
    Surface dst_surface;
    std::tie(dst_surface, dst_rect) =
        res_cache.GetSurfaceSubRect(dst_params, ScaleMatch::Upscale, false);
    if (dst_surface == nullptr)
        return false;

    if (src_surface->is_tiled != dst_surface->is_tiled)
        std::swap(src_rect.top, src_rect.bottom);

    if (config.flip_vertically)
        std::swap(src_rect.top, src_rect.bottom);

    if (!res_cache.BlitSurfaces(src_surface, src_rect, dst_surface, dst_rect))
        return false;

    res_cache.InvalidateRegion(dst_params.addr, dst_params.size, dst_surface);
    return true;
}

bool RasterizerOpenGL::AccelerateTextureCopy(const GPU::Regs::DisplayTransferConfig& config) {
    u32 copy_size = Common::AlignDown(config.texture_copy.size, 16);
    if (copy_size == 0) {
        return false;
    }

    u32 input_gap = config.texture_copy.input_gap * 16;
    u32 input_width = config.texture_copy.input_width * 16;
    if (input_width == 0 && input_gap != 0) {
        return false;
    }
    if (input_gap == 0 || input_width >= copy_size) {
        input_width = copy_size;
        input_gap = 0;
    }
    if (copy_size % input_width != 0) {
        return false;
    }

    u32 output_gap = config.texture_copy.output_gap * 16;
    u32 output_width = config.texture_copy.output_width * 16;
    if (output_width == 0 && output_gap != 0) {
        return false;
    }
    if (output_gap == 0 || output_width >= copy_size) {
        output_width = copy_size;
        output_gap = 0;
    }
    if (copy_size % output_width != 0) {
        return false;
    }

    SurfaceParams src_params;
    src_params.addr = config.GetPhysicalInputAddress();
    src_params.stride = input_width + input_gap; // stride in bytes
    src_params.width = input_width;              // width in bytes
    src_params.height = copy_size / input_width;
    src_params.size = ((src_params.height - 1) * src_params.stride) + src_params.width;
    src_params.end = src_params.addr + src_params.size;

    MathUtil::Rectangle<u32> src_rect;
    Surface src_surface;
    std::tie(src_surface, src_rect) = res_cache.GetTexCopySurface(src_params);
    if (src_surface == nullptr) {
        return false;
    }

    if (output_gap != 0 &&
        (output_width != src_surface->BytesInPixels(src_rect.GetWidth() / src_surface->res_scale) *
                             (src_surface->is_tiled ? 8 : 1) ||
         output_gap % src_surface->BytesInPixels(src_surface->is_tiled ? 64 : 1) != 0)) {
        return false;
    }

    SurfaceParams dst_params = *src_surface;
    dst_params.addr = config.GetPhysicalOutputAddress();
    dst_params.width = src_rect.GetWidth() / src_surface->res_scale;
    dst_params.stride = dst_params.width + src_surface->PixelsInBytes(
                                               src_surface->is_tiled ? output_gap / 8 : output_gap);
    dst_params.height = src_rect.GetHeight() / src_surface->res_scale;
    dst_params.res_scale = src_surface->res_scale;
    dst_params.UpdateParams();

    // Since we are going to invalidate the gap if there is one, we will have to load it first
    const bool load_gap = output_gap != 0;
    MathUtil::Rectangle<u32> dst_rect;
    Surface dst_surface;
    std::tie(dst_surface, dst_rect) =
        res_cache.GetSurfaceSubRect(dst_params, ScaleMatch::Upscale, load_gap);
    if (src_surface == nullptr) {
        return false;
    }

    if (dst_surface->type == SurfaceType::Texture) {
        return false;
    }

    if (!res_cache.BlitSurfaces(src_surface, src_rect, dst_surface, dst_rect)) {
        return false;
    }

    res_cache.InvalidateRegion(dst_params.addr, dst_params.size, dst_surface);
    return true;
}

bool RasterizerOpenGL::AccelerateFill(const GPU::Regs::MemoryFillConfig& config) {
    Surface dst_surface = res_cache.GetFillSurface(config);
    if (dst_surface == nullptr)
        return false;

    res_cache.InvalidateRegion(dst_surface->addr, dst_surface->size, dst_surface);
    return true;
}

bool RasterizerOpenGL::AccelerateDisplay(const GPU::Regs::FramebufferConfig& config,
                                         PAddr framebuffer_addr, u32 pixel_stride,
                                         ScreenInfo& screen_info) {
    if (framebuffer_addr == 0) {
        return false;
    }
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);

    SurfaceParams src_params;
    src_params.addr = framebuffer_addr;
    src_params.width = std::min(config.width.Value(), pixel_stride);
    src_params.height = config.height;
    src_params.stride = pixel_stride;
    src_params.is_tiled = false;
    src_params.pixel_format = SurfaceParams::PixelFormatFromGPUPixelFormat(config.color_format);
    src_params.UpdateParams();

    MathUtil::Rectangle<u32> src_rect;
    Surface src_surface;
    std::tie(src_surface, src_rect) =
        res_cache.GetSurfaceSubRect(src_params, ScaleMatch::Ignore, true);

    if (src_surface == nullptr) {
        return false;
    }

    u32 scaled_width = src_surface->GetScaledWidth();
    u32 scaled_height = src_surface->GetScaledHeight();

    screen_info.display_texcoords = MathUtil::Rectangle<float>(
        (float)src_rect.bottom / (float)scaled_height, (float)src_rect.left / (float)scaled_width,
        (float)src_rect.top / (float)scaled_height, (float)src_rect.right / (float)scaled_width);

    screen_info.display_texture = src_surface->texture.handle;

    return true;
}

void RasterizerOpenGL::SamplerInfo::Create() {
    sampler.Create();
    mag_filter = min_filter = TextureConfig::Linear;
    wrap_s = wrap_t = TextureConfig::Repeat;
    border_color = 0;

    // default is GL_LINEAR_MIPMAP_LINEAR
    glSamplerParameteri(sampler.handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // Other attributes have correct defaults
}

void RasterizerOpenGL::SamplerInfo::SyncWithConfig(
    const Pica::TexturingRegs::TextureConfig& config) {

    GLuint s = sampler.handle;

    if (mag_filter != config.mag_filter) {
        mag_filter = config.mag_filter;
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, PicaToGL::TextureFilterMode(mag_filter));
    }
    if (min_filter != config.min_filter) {
        min_filter = config.min_filter;
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, PicaToGL::TextureFilterMode(min_filter));
    }

    if (wrap_s != config.wrap_s) {
        wrap_s = config.wrap_s;
        glSamplerParameteri(s, GL_TEXTURE_WRAP_S, PicaToGL::WrapMode(wrap_s));
    }
    if (wrap_t != config.wrap_t) {
        wrap_t = config.wrap_t;
        glSamplerParameteri(s, GL_TEXTURE_WRAP_T, PicaToGL::WrapMode(wrap_t));
    }

    if (wrap_s == TextureConfig::ClampToBorder || wrap_t == TextureConfig::ClampToBorder) {
        if (border_color != config.border_color.raw) {
            border_color = config.border_color.raw;
            auto gl_color = PicaToGL::ColorRGBA8(border_color);
            glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, gl_color.data());
        }
    }
}

void RasterizerOpenGL::SetShader() {
    auto config = GLShader::PicaShaderConfig::BuildFromRegs(Pica::g_state.regs);

    // Find (or generate) the GLSL shader for the current TEV state
    auto cached_shader = shader_cache.find(config);
    if (cached_shader != shader_cache.end()) {
        current_shader = &cached_shader->second;
    } else {
        LOG_DEBUG(Render_OpenGL, "Creating new shader");

        auto& shader = shader_cache.emplace(config, PicaShader{}).first->second;
        current_shader = &shader;

        if (has_ARB_separate_shader_objects) {
            shader.shader.Create(nullptr, nullptr,
                                 GLShader::GenerateFragmentShader(config, true).c_str(), {}, true);

            glActiveShaderProgram(pipeline.handle, shader.shader.handle);
        } else {
            shader.shader.Create(GLShader::GenerateDefaultVertexShader(false).c_str(), nullptr,
                                 GLShader::GenerateFragmentShader(config, false).c_str());

            state.draw.shader_program = shader.shader.handle;
            state.Apply();
        }

        // Set the texture samplers to correspond to different texture units
        GLint uniform_tex = glGetUniformLocation(shader.shader.handle, "tex0");
        if (uniform_tex != -1) {
            glUniform1i(uniform_tex, TextureUnits::PicaTexture(0).id);
        }
        uniform_tex = glGetUniformLocation(shader.shader.handle, "tex1");
        if (uniform_tex != -1) {
            glUniform1i(uniform_tex, TextureUnits::PicaTexture(1).id);
        }
        uniform_tex = glGetUniformLocation(shader.shader.handle, "tex2");
        if (uniform_tex != -1) {
            glUniform1i(uniform_tex, TextureUnits::PicaTexture(2).id);
        }

        // Set the texture samplers to correspond to different lookup table texture units
        GLint uniform_lut = glGetUniformLocation(shader.shader.handle, "lighting_lut");
        if (uniform_lut != -1) {
            glUniform1i(uniform_lut, TextureUnits::LightingLUT.id);
        }

        GLint uniform_fog_lut = glGetUniformLocation(shader.shader.handle, "fog_lut");
        if (uniform_fog_lut != -1) {
            glUniform1i(uniform_fog_lut, TextureUnits::FogLUT.id);
        }

        GLint uniform_proctex_noise_lut =
            glGetUniformLocation(shader.shader.handle, "proctex_noise_lut");
        if (uniform_proctex_noise_lut != -1) {
            glUniform1i(uniform_proctex_noise_lut, TextureUnits::ProcTexNoiseLUT.id);
        }

        GLint uniform_proctex_color_map =
            glGetUniformLocation(shader.shader.handle, "proctex_color_map");
        if (uniform_proctex_color_map != -1) {
            glUniform1i(uniform_proctex_color_map, TextureUnits::ProcTexColorMap.id);
        }

        GLint uniform_proctex_alpha_map =
            glGetUniformLocation(shader.shader.handle, "proctex_alpha_map");
        if (uniform_proctex_alpha_map != -1) {
            glUniform1i(uniform_proctex_alpha_map, TextureUnits::ProcTexAlphaMap.id);
        }

        GLint uniform_proctex_lut = glGetUniformLocation(shader.shader.handle, "proctex_lut");
        if (uniform_proctex_lut != -1) {
            glUniform1i(uniform_proctex_lut, TextureUnits::ProcTexLUT.id);
        }

        GLint uniform_proctex_diff_lut =
            glGetUniformLocation(shader.shader.handle, "proctex_diff_lut");
        if (uniform_proctex_diff_lut != -1) {
            glUniform1i(uniform_proctex_diff_lut, TextureUnits::ProcTexDiffLUT.id);
        }

        SetShaderUniformBlockBindings(shader.shader.handle);

        // TODO: why is this here ???
        // Update uniforms
        SyncDepthScale();
        SyncDepthOffset();
        SyncAlphaTest();
        SyncCombinerColor();
        auto& tev_stages = Pica::g_state.regs.texturing.GetTevStages();
        for (int index = 0; index < tev_stages.size(); ++index)
            SyncTevConstColor(index, tev_stages[index]);

        SyncGlobalAmbient();
        for (int light_index = 0; light_index < 8; light_index++) {
            SyncLightSpecular0(light_index);
            SyncLightSpecular1(light_index);
            SyncLightDiffuse(light_index);
            SyncLightAmbient(light_index);
            SyncLightPosition(light_index);
            SyncLightDistanceAttenuationBias(light_index);
            SyncLightDistanceAttenuationScale(light_index);
        }

        SyncFogColor();
        SyncProcTexNoise();
    }
}

void RasterizerOpenGL::SyncClipEnabled() {
    state.clip_distance[1] = Pica::g_state.regs.rasterizer.clip_enable != 0;
}

void RasterizerOpenGL::SyncClipCoef() {
    const auto raw_clip_coef = Pica::g_state.regs.rasterizer.GetClipCoef();
    const GLvec4 new_clip_coef = {raw_clip_coef.x.ToFloat32(), raw_clip_coef.y.ToFloat32(),
                                  raw_clip_coef.z.ToFloat32(), raw_clip_coef.w.ToFloat32()};
    if (new_clip_coef != uniform_block_data.data.clip_coef) {
        uniform_block_data.data.clip_coef = new_clip_coef;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncCullMode() {
    const auto& regs = Pica::g_state.regs;

    switch (regs.rasterizer.cull_mode) {
    case Pica::RasterizerRegs::CullMode::KeepAll:
        state.cull.enabled = false;
        break;

    case Pica::RasterizerRegs::CullMode::KeepClockWise:
        state.cull.enabled = true;
        state.cull.front_face = GL_CW;
        break;

    case Pica::RasterizerRegs::CullMode::KeepCounterClockWise:
        state.cull.enabled = true;
        state.cull.front_face = GL_CCW;
        break;

    default:
        LOG_CRITICAL(Render_OpenGL, "Unknown cull mode %u",
                     static_cast<u32>(regs.rasterizer.cull_mode.Value()));
        UNIMPLEMENTED();
        break;
    }
}

void RasterizerOpenGL::SyncDepthScale() {
    float depth_scale =
        Pica::float24::FromRaw(Pica::g_state.regs.rasterizer.viewport_depth_range).ToFloat32();
    if (depth_scale != uniform_block_data.data.depth_scale) {
        uniform_block_data.data.depth_scale = depth_scale;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncDepthOffset() {
    float depth_offset =
        Pica::float24::FromRaw(Pica::g_state.regs.rasterizer.viewport_depth_near_plane).ToFloat32();
    if (depth_offset != uniform_block_data.data.depth_offset) {
        uniform_block_data.data.depth_offset = depth_offset;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncBlendEnabled() {
    state.blend.enabled = (Pica::g_state.regs.framebuffer.output_merger.alphablend_enable == 1);
}

void RasterizerOpenGL::SyncBlendFuncs() {
    const auto& regs = Pica::g_state.regs;
    state.blend.rgb_equation =
        PicaToGL::BlendEquation(regs.framebuffer.output_merger.alpha_blending.blend_equation_rgb);
    state.blend.a_equation =
        PicaToGL::BlendEquation(regs.framebuffer.output_merger.alpha_blending.blend_equation_a);
    state.blend.src_rgb_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_source_rgb);
    state.blend.dst_rgb_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_dest_rgb);
    state.blend.src_a_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_source_a);
    state.blend.dst_a_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_dest_a);
}

void RasterizerOpenGL::SyncBlendColor() {
    auto blend_color =
        PicaToGL::ColorRGBA8(Pica::g_state.regs.framebuffer.output_merger.blend_const.raw);
    state.blend.color.red = blend_color[0];
    state.blend.color.green = blend_color[1];
    state.blend.color.blue = blend_color[2];
    state.blend.color.alpha = blend_color[3];
}

void RasterizerOpenGL::SyncFogColor() {
    const auto& regs = Pica::g_state.regs;
    uniform_block_data.data.fog_color = {
        regs.texturing.fog_color.r.Value() / 255.0f,
        regs.texturing.fog_color.g.Value() / 255.0f,
        regs.texturing.fog_color.b.Value() / 255.0f,
    };
    uniform_block_data.dirty = true;
}

void RasterizerOpenGL::SyncFogLUT() {
    std::array<GLvec2, 128> new_data;

    std::transform(Pica::g_state.fog.lut.begin(), Pica::g_state.fog.lut.end(), new_data.begin(),
                   [](const auto& entry) {
                       return GLvec2{entry.ToFloat(), entry.DiffToFloat()};
                   });

    if (new_data != fog_lut_data) {
        fog_lut_data = new_data;
        glBindBuffer(GL_TEXTURE_BUFFER, fog_lut_buffer.handle);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, new_data.size() * sizeof(GLvec2), new_data.data());
    }
}

void RasterizerOpenGL::SyncProcTexNoise() {
    const auto& regs = Pica::g_state.regs.texturing;
    uniform_block_data.data.proctex_noise_f = {
        Pica::float16::FromRaw(regs.proctex_noise_frequency.u).ToFloat32(),
        Pica::float16::FromRaw(regs.proctex_noise_frequency.v).ToFloat32(),
    };
    uniform_block_data.data.proctex_noise_a = {
        regs.proctex_noise_u.amplitude / 4095.0f,
        regs.proctex_noise_v.amplitude / 4095.0f,
    };
    uniform_block_data.data.proctex_noise_p = {
        Pica::float16::FromRaw(regs.proctex_noise_u.phase).ToFloat32(),
        Pica::float16::FromRaw(regs.proctex_noise_v.phase).ToFloat32(),
    };

    uniform_block_data.dirty = true;
}

// helper function for SyncProcTexNoiseLUT/ColorMap/AlphaMap
static void SyncProcTexValueLUT(const std::array<Pica::State::ProcTex::ValueEntry, 128>& lut,
                                std::array<GLvec2, 128>& lut_data, GLuint buffer) {
    std::array<GLvec2, 128> new_data;
    std::transform(lut.begin(), lut.end(), new_data.begin(), [](const auto& entry) {
        return GLvec2{entry.ToFloat(), entry.DiffToFloat()};
    });

    if (new_data != lut_data) {
        lut_data = new_data;
        glBindBuffer(GL_TEXTURE_BUFFER, buffer);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, new_data.size() * sizeof(GLvec2), new_data.data());
    }
}

void RasterizerOpenGL::SyncProcTexNoiseLUT() {
    SyncProcTexValueLUT(Pica::g_state.proctex.noise_table, proctex_noise_lut_data,
                        proctex_noise_lut_buffer.handle);
}

void RasterizerOpenGL::SyncProcTexColorMap() {
    SyncProcTexValueLUT(Pica::g_state.proctex.color_map_table, proctex_color_map_data,
                        proctex_color_map_buffer.handle);
}

void RasterizerOpenGL::SyncProcTexAlphaMap() {
    SyncProcTexValueLUT(Pica::g_state.proctex.alpha_map_table, proctex_alpha_map_data,
                        proctex_alpha_map_buffer.handle);
}

void RasterizerOpenGL::SyncProcTexLUT() {
    std::array<GLvec4, 256> new_data;

    std::transform(Pica::g_state.proctex.color_table.begin(),
                   Pica::g_state.proctex.color_table.end(), new_data.begin(),
                   [](const auto& entry) {
                       auto rgba = entry.ToVector() / 255.0f;
                       return GLvec4{rgba.r(), rgba.g(), rgba.b(), rgba.a()};
                   });

    if (new_data != proctex_lut_data) {
        proctex_lut_data = new_data;
        glBindBuffer(GL_TEXTURE_BUFFER, proctex_lut_buffer.handle);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, new_data.size() * sizeof(GLvec4), new_data.data());
    }
}

void RasterizerOpenGL::SyncProcTexDiffLUT() {
    std::array<GLvec4, 256> new_data;

    std::transform(Pica::g_state.proctex.color_diff_table.begin(),
                   Pica::g_state.proctex.color_diff_table.end(), new_data.begin(),
                   [](const auto& entry) {
                       auto rgba = entry.ToVector() / 255.0f;
                       return GLvec4{rgba.r(), rgba.g(), rgba.b(), rgba.a()};
                   });

    if (new_data != proctex_diff_lut_data) {
        proctex_diff_lut_data = new_data;
        glBindBuffer(GL_TEXTURE_BUFFER, proctex_diff_lut_buffer.handle);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, new_data.size() * sizeof(GLvec4), new_data.data());
    }
}

void RasterizerOpenGL::SyncAlphaTest() {
    const auto& regs = Pica::g_state.regs;
    if (regs.framebuffer.output_merger.alpha_test.ref != uniform_block_data.data.alphatest_ref) {
        uniform_block_data.data.alphatest_ref = regs.framebuffer.output_merger.alpha_test.ref;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLogicOp() {
    state.logic_op = PicaToGL::LogicOp(Pica::g_state.regs.framebuffer.output_merger.logic_op);
}

void RasterizerOpenGL::SyncColorWriteMask() {
    const auto& regs = Pica::g_state.regs;

    auto IsColorWriteEnabled = [&](u32 value) {
        return (regs.framebuffer.framebuffer.allow_color_write != 0 && value != 0) ? GL_TRUE
                                                                                   : GL_FALSE;
    };

    state.color_mask.red_enabled = IsColorWriteEnabled(regs.framebuffer.output_merger.red_enable);
    state.color_mask.green_enabled =
        IsColorWriteEnabled(regs.framebuffer.output_merger.green_enable);
    state.color_mask.blue_enabled = IsColorWriteEnabled(regs.framebuffer.output_merger.blue_enable);
    state.color_mask.alpha_enabled =
        IsColorWriteEnabled(regs.framebuffer.output_merger.alpha_enable);
}

void RasterizerOpenGL::SyncStencilWriteMask() {
    const auto& regs = Pica::g_state.regs;
    state.stencil.write_mask =
        (regs.framebuffer.framebuffer.allow_depth_stencil_write != 0)
            ? static_cast<GLuint>(regs.framebuffer.output_merger.stencil_test.write_mask)
            : 0;
}

void RasterizerOpenGL::SyncDepthWriteMask() {
    const auto& regs = Pica::g_state.regs;
    state.depth.write_mask = (regs.framebuffer.framebuffer.allow_depth_stencil_write != 0 &&
                              regs.framebuffer.output_merger.depth_write_enable)
                                 ? GL_TRUE
                                 : GL_FALSE;
}

void RasterizerOpenGL::SyncStencilTest() {
    const auto& regs = Pica::g_state.regs;
    state.stencil.test_enabled =
        regs.framebuffer.output_merger.stencil_test.enable &&
        regs.framebuffer.framebuffer.depth_format == Pica::FramebufferRegs::DepthFormat::D24S8;
    state.stencil.test_func =
        PicaToGL::CompareFunc(regs.framebuffer.output_merger.stencil_test.func);
    state.stencil.test_ref = regs.framebuffer.output_merger.stencil_test.reference_value;
    state.stencil.test_mask = regs.framebuffer.output_merger.stencil_test.input_mask;
    state.stencil.action_stencil_fail =
        PicaToGL::StencilOp(regs.framebuffer.output_merger.stencil_test.action_stencil_fail);
    state.stencil.action_depth_fail =
        PicaToGL::StencilOp(regs.framebuffer.output_merger.stencil_test.action_depth_fail);
    state.stencil.action_depth_pass =
        PicaToGL::StencilOp(regs.framebuffer.output_merger.stencil_test.action_depth_pass);
}

void RasterizerOpenGL::SyncDepthTest() {
    const auto& regs = Pica::g_state.regs;
    state.depth.test_enabled = regs.framebuffer.output_merger.depth_test_enable == 1 ||
                               regs.framebuffer.output_merger.depth_write_enable == 1;
    state.depth.test_func =
        regs.framebuffer.output_merger.depth_test_enable == 1
            ? PicaToGL::CompareFunc(regs.framebuffer.output_merger.depth_test_func)
            : GL_ALWAYS;
}

void RasterizerOpenGL::SyncCombinerColor() {
    auto combiner_color =
        PicaToGL::ColorRGBA8(Pica::g_state.regs.texturing.tev_combiner_buffer_color.raw);
    if (combiner_color != uniform_block_data.data.tev_combiner_buffer_color) {
        uniform_block_data.data.tev_combiner_buffer_color = combiner_color;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncTevConstColor(int stage_index,
                                         const Pica::TexturingRegs::TevStageConfig& tev_stage) {
    auto const_color = PicaToGL::ColorRGBA8(tev_stage.const_color);
    if (const_color != uniform_block_data.data.const_color[stage_index]) {
        uniform_block_data.data.const_color[stage_index] = const_color;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncGlobalAmbient() {
    auto color = PicaToGL::LightColor(Pica::g_state.regs.lighting.global_ambient);
    if (color != uniform_block_data.data.lighting_global_ambient) {
        uniform_block_data.data.lighting_global_ambient = color;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightingLUT(unsigned lut_index) {
    std::array<GLvec2, 256> new_data;
    const auto& source_lut = Pica::g_state.lighting.luts[lut_index];
    std::transform(source_lut.begin(), source_lut.end(), new_data.begin(), [](const auto& entry) {
        return GLvec2{entry.ToFloat(), entry.DiffToFloat()};
    });

    if (new_data != lighting_lut_data[lut_index]) {
        lighting_lut_data[lut_index] = new_data;
        glBindBuffer(GL_TEXTURE_BUFFER, lighting_lut_buffer.handle);
        glBufferSubData(GL_TEXTURE_BUFFER, lut_index * new_data.size() * sizeof(GLvec2),
                        new_data.size() * sizeof(GLvec2), new_data.data());
    }
}

void RasterizerOpenGL::SyncLightSpecular0(int light_index) {
    auto color = PicaToGL::LightColor(Pica::g_state.regs.lighting.light[light_index].specular_0);
    if (color != uniform_block_data.data.light_src[light_index].specular_0) {
        uniform_block_data.data.light_src[light_index].specular_0 = color;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightSpecular1(int light_index) {
    auto color = PicaToGL::LightColor(Pica::g_state.regs.lighting.light[light_index].specular_1);
    if (color != uniform_block_data.data.light_src[light_index].specular_1) {
        uniform_block_data.data.light_src[light_index].specular_1 = color;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightDiffuse(int light_index) {
    auto color = PicaToGL::LightColor(Pica::g_state.regs.lighting.light[light_index].diffuse);
    if (color != uniform_block_data.data.light_src[light_index].diffuse) {
        uniform_block_data.data.light_src[light_index].diffuse = color;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightAmbient(int light_index) {
    auto color = PicaToGL::LightColor(Pica::g_state.regs.lighting.light[light_index].ambient);
    if (color != uniform_block_data.data.light_src[light_index].ambient) {
        uniform_block_data.data.light_src[light_index].ambient = color;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightPosition(int light_index) {
    GLvec3 position = {
        Pica::float16::FromRaw(Pica::g_state.regs.lighting.light[light_index].x).ToFloat32(),
        Pica::float16::FromRaw(Pica::g_state.regs.lighting.light[light_index].y).ToFloat32(),
        Pica::float16::FromRaw(Pica::g_state.regs.lighting.light[light_index].z).ToFloat32()};

    if (position != uniform_block_data.data.light_src[light_index].position) {
        uniform_block_data.data.light_src[light_index].position = position;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightSpotDirection(int light_index) {
    const auto& light = Pica::g_state.regs.lighting.light[light_index];
    GLvec3 spot_direction = {light.spot_x / 2047.0f, light.spot_y / 2047.0f,
                             light.spot_z / 2047.0f};

    if (spot_direction != uniform_block_data.data.light_src[light_index].spot_direction) {
        uniform_block_data.data.light_src[light_index].spot_direction = spot_direction;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightDistanceAttenuationBias(int light_index) {
    GLfloat dist_atten_bias =
        Pica::float20::FromRaw(Pica::g_state.regs.lighting.light[light_index].dist_atten_bias)
            .ToFloat32();

    if (dist_atten_bias != uniform_block_data.data.light_src[light_index].dist_atten_bias) {
        uniform_block_data.data.light_src[light_index].dist_atten_bias = dist_atten_bias;
        uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLightDistanceAttenuationScale(int light_index) {
    GLfloat dist_atten_scale =
        Pica::float20::FromRaw(Pica::g_state.regs.lighting.light[light_index].dist_atten_scale)
            .ToFloat32();

    if (dist_atten_scale != uniform_block_data.data.light_src[light_index].dist_atten_scale) {
        uniform_block_data.data.light_src[light_index].dist_atten_scale = dist_atten_scale;
        uniform_block_data.dirty = true;
    }
}
