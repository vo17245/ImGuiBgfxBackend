// Dear ImGui renderer backend for bgfx. Pair with any ImGui platform backend.
#pragma once
#include "imgui.h"
#ifndef IMGUI_DISABLE
#include <bgfx/bgfx.h>

struct ImGui_ImplBgfx_InitInfo
{
    bgfx::ViewId ViewId = 255; // Reserve this view for ImGui; rendering order is sequential.
    bgfx::FrameBufferHandle FrameBuffer = BGFX_INVALID_HANDLE; // Invalid = main swap chain.
};

// bgfx must already be initialized. All functions run on its API thread.
IMGUI_IMPL_API bool ImGui_ImplBgfx_Init(const ImGui_ImplBgfx_InitInfo& info = {});
IMGUI_IMPL_API void ImGui_ImplBgfx_Shutdown(); // Call before bgfx::shutdown()/DestroyContext().
IMGUI_IMPL_API bool ImGui_ImplBgfx_NewFrame();
// Does not clear the view or call bgfx::frame(). False = resource/texture upload failure.
IMGUI_IMPL_API bool ImGui_ImplBgfx_RenderDrawData(ImDrawData* draw_data);
IMGUI_IMPL_API bool ImGui_ImplBgfx_CreateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplBgfx_InvalidateDeviceObjects();
IMGUI_IMPL_API bool ImGui_ImplBgfx_UpdateTexture(ImTextureData* texture);
IMGUI_IMPL_API void ImGui_ImplBgfx_SetFrameBuffer(bgfx::FrameBufferHandle framebuffer);

// IDs encode handle.idx + 1. External textures remain owned by the caller.
// Only regular, single-sample 2D color textures are supported.
IMGUI_IMPL_API ImTextureID ImGui_ImplBgfx_TextureID(bgfx::TextureHandle texture);
IMGUI_IMPL_API bgfx::TextureHandle ImGui_ImplBgfx_TextureHandle(ImTextureID texture_id);

// Available in GetPlatformIO().Renderer_RenderState during custom draw callbacks.
// A callback may issue bgfx commands through Encoder; the next UI draw rebinds its state.
struct ImGui_ImplBgfx_RenderState
{
    bgfx::Encoder* Encoder;
    bgfx::ViewId ViewId;
    bgfx::ProgramHandle Program;
    bgfx::UniformHandle Sampler;
    uint32_t SamplerFlags;
};
#endif
