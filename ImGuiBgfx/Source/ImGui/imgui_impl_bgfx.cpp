// Dear ImGui renderer backend for bgfx.
// Supports texture requests (dynamic fonts), user textures, VtxOffset/IdxOffset,
// framebuffer scaling, reset/sampler callbacks and 16-/32-bit indices.
// Multiple OS viewports are not implemented.
#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_bgfx.h"
#include <bgfx/embedded_shader.h>
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

// Supplied by the matching bgfx checkout through the CMake target's private include path.
#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"

static const bgfx::EmbeddedShader ImGui_ImplBgfx_Shaders[] =
{
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END()
};

static constexpr uint32_t ImGui_ImplBgfx_Linear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
static constexpr uint32_t ImGui_ImplBgfx_Nearest = ImGui_ImplBgfx_Linear
    | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

struct ImGui_ImplBgfx_Data
{
    ImGui_ImplBgfx_InitInfo Info;
    bgfx::VertexLayout Layout;
    bgfx::ProgramHandle Program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle Sampler = BGFX_INVALID_HANDLE;
    ImGui_ImplBgfx_RenderState* RenderState = nullptr;
};
struct ImGui_ImplBgfx_Texture { bgfx::TextureHandle Handle; };

static ImGui_ImplBgfx_Data* ImGui_ImplBgfx_GetBackendData()
{
    return ImGui::GetCurrentContext()
        ? static_cast<ImGui_ImplBgfx_Data*>(ImGui::GetIO().BackendRendererUserData) : nullptr;
}

ImTextureID ImGui_ImplBgfx_TextureID(bgfx::TextureHandle texture)
{
    return bgfx::isValid(texture) ? static_cast<ImTextureID>(texture.idx) + 1 : ImTextureID_Invalid;
}
bgfx::TextureHandle ImGui_ImplBgfx_TextureHandle(ImTextureID texture_id)
{
    if (texture_id == ImTextureID_Invalid || texture_id == 0 || texture_id > UINT16_MAX)
        return BGFX_INVALID_HANDLE;
    return { static_cast<uint16_t>(texture_id - 1) };
}

static void ImGui_ImplBgfx_ResetCallback(const ImDrawList*, const ImDrawCmd*) {}
static void ImGui_ImplBgfx_LinearCallback(const ImDrawList*, const ImDrawCmd*)
{
    auto* bd = ImGui_ImplBgfx_GetBackendData();
    if (bd->RenderState) bd->RenderState->SamplerFlags = ImGui_ImplBgfx_Linear;
}
static void ImGui_ImplBgfx_NearestCallback(const ImDrawList*, const ImDrawCmd*)
{
    auto* bd = ImGui_ImplBgfx_GetBackendData();
    if (bd->RenderState) bd->RenderState->SamplerFlags = ImGui_ImplBgfx_Nearest;
}

bool ImGui_ImplBgfx_CreateDeviceObjects()
{
    auto* bd = ImGui_ImplBgfx_GetBackendData();
    IM_ASSERT(bd);
    if (bgfx::isValid(bd->Program)) return true;
    const auto type = bgfx::getRendererType();
    // The upstream embedded Noop shader uses an obsolete binary format.
    if (type == bgfx::RendererType::Noop) return false;
    auto vs = bgfx::createEmbeddedShader(ImGui_ImplBgfx_Shaders, type, "vs_ocornut_imgui");
    auto fs = bgfx::createEmbeddedShader(ImGui_ImplBgfx_Shaders, type, "fs_ocornut_imgui");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs))
    {
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        return false;
    }
    bd->Program = bgfx::createProgram(vs, fs, false);
    bgfx::destroy(vs);
    bgfx::destroy(fs);
    if (!bgfx::isValid(bd->Program)) return false;
    bd->Sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(bd->Sampler))
    {
        bgfx::destroy(bd->Program);
        bd->Program = BGFX_INVALID_HANDLE;
        return false;
    }
    return true;
}

static void ImGui_ImplBgfx_DestroyTexture(ImTextureData* texture)
{
    auto* backend_texture = static_cast<ImGui_ImplBgfx_Texture*>(texture->BackendUserData);
    if (backend_texture)
    {
        bgfx::destroy(backend_texture->Handle); // bgfx defers actual deletion until submitted work completes.
        IM_DELETE(backend_texture);
        texture->BackendUserData = nullptr;
        texture->SetTexID(ImTextureID_Invalid);
    }
    texture->SetStatus(ImTextureStatus_Destroyed);
}

// Copy CPU pixels into bgfx-owned memory; never enqueue pointers into an ImGui atlas.
// Convert Alpha8 to white RGBA so the same shader handles both atlas formats.
static const bgfx::Memory* ImGui_ImplBgfx_CopyPixels(ImTextureData* texture, int x, int y, int width, int height)
{
    const auto* src = static_cast<const unsigned char*>(texture->GetPixelsAt(x, y));
    const uint32_t row_bytes = static_cast<uint32_t>(width) * 4;
    const bgfx::Memory* memory = bgfx::alloc(row_bytes * static_cast<uint32_t>(height));
    for (int row = 0; row < height; ++row)
    {
        unsigned char* dst = memory->data + row * row_bytes;
        if (texture->Format == ImTextureFormat_RGBA32)
            std::memcpy(dst, src, row_bytes);
        else
            for (int column = 0; column < width; ++column)
            {
                dst[column * 4 + 0] = dst[column * 4 + 1] = dst[column * 4 + 2] = 255;
                dst[column * 4 + 3] = src[column];
            }
        src += texture->GetPitch();
    }
    return memory;
}

bool ImGui_ImplBgfx_UpdateTexture(ImTextureData* texture)
{
    IM_ASSERT(ImGui_ImplBgfx_GetBackendData() && texture);
    if (texture->Status == ImTextureStatus_WantDestroy)
    {
        if (texture->UnusedFrames > 0) ImGui_ImplBgfx_DestroyTexture(texture);
        return true;
    }
    if (texture->Status != ImTextureStatus_WantCreate && texture->Status != ImTextureStatus_WantUpdates)
        return true;
    const auto max_size = std::min<uint32_t>(bgfx::getCaps()->limits.maxTextureSize, UINT16_MAX);
    if (texture->Width <= 0 || texture->Height <= 0
        || static_cast<uint32_t>(texture->Width) > max_size || static_cast<uint32_t>(texture->Height) > max_size
        || uint64_t(texture->Width) * texture->Height * 4 > UINT32_MAX
        || (texture->Format != ImTextureFormat_RGBA32 && texture->Format != ImTextureFormat_Alpha8))
        return false;

    if (texture->Status == ImTextureStatus_WantCreate)
    {
        IM_ASSERT(texture->GetTexID() == ImTextureID_Invalid && texture->BackendUserData == nullptr);
        auto handle = bgfx::createTexture2D(static_cast<uint16_t>(texture->Width), static_cast<uint16_t>(texture->Height),
            false, 1, bgfx::TextureFormat::RGBA8, ImGui_ImplBgfx_Linear);
        if (!bgfx::isValid(handle)) return false;
        // Supplying initial pixels to createTexture2D makes it immutable in bgfx.
        bgfx::updateTexture2D(handle, 0, 0, 0, 0, static_cast<uint16_t>(texture->Width),
            static_cast<uint16_t>(texture->Height), ImGui_ImplBgfx_CopyPixels(texture, 0, 0, texture->Width, texture->Height));
        auto* backend_texture = IM_NEW(ImGui_ImplBgfx_Texture)();
        backend_texture->Handle = handle;
        texture->BackendUserData = backend_texture;
        texture->SetTexID(ImGui_ImplBgfx_TextureID(handle));
        bgfx::setName(handle, "ImGui texture");
    }
    else
    {
        auto* backend_texture = static_cast<ImGui_ImplBgfx_Texture*>(texture->BackendUserData);
        if (!backend_texture) return false;
        // Validate all rectangles before submitting any updates.
        for (const ImTextureRect& rect : texture->Updates)
            if (int(rect.x) + rect.w > texture->Width || int(rect.y) + rect.h > texture->Height)
                return false;
        for (const ImTextureRect& rect : texture->Updates)
        {
            if (rect.w == 0 || rect.h == 0) continue;
            bgfx::updateTexture2D(backend_texture->Handle, 0, 0, rect.x, rect.y, rect.w, rect.h,
                ImGui_ImplBgfx_CopyPixels(texture, rect.x, rect.y, rect.w, rect.h));
        }
    }
    texture->SetStatus(ImTextureStatus_OK);
    return true;
}

void ImGui_ImplBgfx_InvalidateDeviceObjects()
{
    auto* bd = ImGui_ImplBgfx_GetBackendData();
    IM_ASSERT(bd);
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        if (texture->RefCount == 1 && texture->BackendUserData)
            ImGui_ImplBgfx_DestroyTexture(texture);
    if (bgfx::isValid(bd->Sampler)) bgfx::destroy(bd->Sampler);
    if (bgfx::isValid(bd->Program)) bgfx::destroy(bd->Program);
    bd->Sampler = BGFX_INVALID_HANDLE;
    bd->Program = BGFX_INVALID_HANDLE;
}

bool ImGui_ImplBgfx_Init(const ImGui_ImplBgfx_InitInfo& info)
{
    IMGUI_CHECKVERSION();
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr);
    if (info.ViewId >= bgfx::getCaps()->limits.maxViews) return false;
    auto* bd = IM_NEW(ImGui_ImplBgfx_Data)();
    bd->Info = info;
    static_assert(sizeof(ImDrawVert) == 20 && offsetof(ImDrawVert, pos) == 0
        && offsetof(ImDrawVert, uv) == 8 && offsetof(ImDrawVert, col) == 16,
        "Update the bgfx vertex layout for a custom ImDrawVert.");
    bd->Layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
    io.BackendRendererUserData = bd;
    if (!ImGui_ImplBgfx_CreateDeviceObjects())
    {
        IM_DELETE(bd);
        io.BackendRendererUserData = nullptr;
        return false;
    }
    io.BackendRendererName = "imgui_impl_bgfx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    auto& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight =
        static_cast<int>(std::min<uint32_t>(bgfx::getCaps()->limits.maxTextureSize, UINT16_MAX));
    platform_io.DrawCallback_ResetRenderState = ImGui_ImplBgfx_ResetCallback;
    platform_io.DrawCallback_SetSamplerLinear = ImGui_ImplBgfx_LinearCallback;
    platform_io.DrawCallback_SetSamplerNearest = ImGui_ImplBgfx_NearestCallback;
    return true;
}

void ImGui_ImplBgfx_Shutdown()
{
    auto* bd = ImGui_ImplBgfx_GetBackendData();
    IM_ASSERT(bd);
    ImGui_ImplBgfx_InvalidateDeviceObjects();
    auto& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    ImGui::GetPlatformIO().ClearRendererHandlers();
    IM_DELETE(bd);
}
bool ImGui_ImplBgfx_NewFrame()
{
    IM_ASSERT(ImGui_ImplBgfx_GetBackendData());
    return ImGui_ImplBgfx_CreateDeviceObjects();
}
void ImGui_ImplBgfx_SetFrameBuffer(bgfx::FrameBufferHandle framebuffer)
{
    IM_ASSERT(ImGui_ImplBgfx_GetBackendData());
    ImGui_ImplBgfx_GetBackendData()->Info.FrameBuffer = framebuffer;
}

static void ImGui_ImplBgfx_SetupView(ImGui_ImplBgfx_Data* bd, const ImDrawData* draw_data, uint16_t width, uint16_t height)
{
    const float left = draw_data->DisplayPos.x;
    const float top = draw_data->DisplayPos.y;
    float projection[16];
    bx::mtxOrtho(projection, left, left + draw_data->DisplaySize.x,
        top + draw_data->DisplaySize.y, top, 0.0f, 1000.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewName(bd->Info.ViewId, "ImGui");
    bgfx::setViewMode(bd->Info.ViewId, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(bd->Info.ViewId, bd->Info.FrameBuffer);
    bgfx::setViewRect(bd->Info.ViewId, 0, 0, width, height);
    bgfx::setViewTransform(bd->Info.ViewId, nullptr, projection);
}

bool ImGui_ImplBgfx_RenderDrawData(ImDrawData* draw_data)
{
    auto* bd = ImGui_ImplBgfx_GetBackendData();
    IM_ASSERT(bd);
    if (!draw_data) return true;
    if (!ImGui_ImplBgfx_CreateDeviceObjects()) return false;
    // Service texture requests even for an empty/minimized frame.
    if (draw_data->Textures)
        for (ImTextureData* texture : *draw_data->Textures)
            if (texture->Status != ImTextureStatus_OK && !ImGui_ImplBgfx_UpdateTexture(texture))
                return false;
    const float fb_width = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
    const float fb_height = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
    if (fb_width <= 0 || fb_height <= 0) return true;
    if (!std::isfinite(fb_width) || !std::isfinite(fb_height) || fb_width > UINT16_MAX || fb_height > UINT16_MAX)
        return false;
    const auto width = static_cast<uint16_t>(fb_width);
    const auto height = static_cast<uint16_t>(fb_height);
    if (width == 0 || height == 0) return true;

    bgfx::TransientVertexBuffer vertices{};
    bgfx::TransientIndexBuffer indices{};
    if (draw_data->TotalVtxCount > 0 && draw_data->TotalIdxCount > 0)
    {
        // Allocate atomically for the entire UI: never silently render half a frame.
        if (!bgfx::allocTransientBuffers(&vertices, bd->Layout, draw_data->TotalVtxCount,
                &indices, draw_data->TotalIdxCount, sizeof(ImDrawIdx) == 4))
            return false;
        uint32_t vertex_offset = 0, index_offset = 0;
        for (const ImDrawList* list : draw_data->CmdLists)
        {
            if (list->VtxBuffer.Size > 0) std::memcpy(vertices.data + vertex_offset * sizeof(ImDrawVert), list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
            if (list->IdxBuffer.Size > 0) std::memcpy(indices.data + index_offset * sizeof(ImDrawIdx), list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
#ifdef IMGUI_USE_BGRA_PACKED_COLOR
            auto* dst = reinterpret_cast<ImDrawVert*>(vertices.data) + vertex_offset;
            for (int i = 0; i < list->VtxBuffer.Size; ++i)
                dst[i].col = (dst[i].col & 0xff00ff00u) | ((dst[i].col & 0x00ff0000u) >> 16) | ((dst[i].col & 0x000000ffu) << 16);
#endif
            vertex_offset += list->VtxBuffer.Size;
            index_offset += list->IdxBuffer.Size;
        }
    }
    ImGui_ImplBgfx_SetupView(bd, draw_data, width, height);
    bgfx::Encoder* encoder = bgfx::begin();
    ImGui_ImplBgfx_RenderState render_state{encoder, bd->Info.ViewId, bd->Program, bd->Sampler, ImGui_ImplBgfx_Linear};
    ImGui::GetPlatformIO().Renderer_RenderState = bd->RenderState = &render_state;
    bool success = true;
    uint32_t global_vertex_offset = 0, global_index_offset = 0;
    for (const ImDrawList* list : draw_data->CmdLists)
    {
        for (const ImDrawCmd& cmd : list->CmdBuffer)
        {
            if (cmd.UserCallback)
            {
                bool reset = cmd.UserCallback == ImGui_ImplBgfx_ResetCallback;
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
                reset = reset || cmd.UserCallback == ImDrawCallback_ResetRenderState;
#endif
                if (reset)
                {
                    encoder->discard();
                    render_state.SamplerFlags = ImGui_ImplBgfx_Linear;
                    ImGui_ImplBgfx_SetupView(bd, draw_data, width, height);
                }
                else
                    cmd.UserCallback(list, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0) continue;
            const auto texture = ImGui_ImplBgfx_TextureHandle(cmd.GetTexID());
            if (!bgfx::isValid(texture) || uint64_t(cmd.IdxOffset) + cmd.ElemCount > static_cast<uint32_t>(list->IdxBuffer.Size)
                || cmd.VtxOffset >= static_cast<uint32_t>(list->VtxBuffer.Size))
            {
                success = false;
                continue;
            }
            const float min_x = std::clamp((cmd.ClipRect.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x, 0.0f, float(width));
            const float min_y = std::clamp((cmd.ClipRect.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y, 0.0f, float(height));
            const float max_x = std::clamp((cmd.ClipRect.z - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x, 0.0f, float(width));
            const float max_y = std::clamp((cmd.ClipRect.w - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y, 0.0f, float(height));
            if (!(max_x > min_x && max_y > min_y)) continue;
            const auto x = static_cast<uint16_t>(min_x);
            const auto y = static_cast<uint16_t>(min_y);
            encoder->setScissor(x, y, static_cast<uint16_t>(std::ceil(max_x) - x), static_cast<uint16_t>(std::ceil(max_y) - y));
            encoder->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
                | BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                    BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            encoder->setStencil(BGFX_STENCIL_NONE);
            encoder->setTransform(nullptr);
            encoder->setVertexBuffer(0, &vertices, global_vertex_offset + cmd.VtxOffset, list->VtxBuffer.Size - cmd.VtxOffset);
            encoder->setIndexBuffer(&indices, global_index_offset + cmd.IdxOffset, cmd.ElemCount);
            encoder->setTexture(0, bd->Sampler, texture, render_state.SamplerFlags);
            encoder->submit(bd->Info.ViewId, bd->Program);
        }
        global_vertex_offset += list->VtxBuffer.Size;
        global_index_offset += list->IdxBuffer.Size;
    }
    encoder->discard();
    bgfx::end(encoder);
    bd->RenderState = nullptr;
    ImGui::GetPlatformIO().Renderer_RenderState = nullptr;
    return success;
}
#endif
