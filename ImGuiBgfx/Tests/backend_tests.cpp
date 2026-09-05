#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_bgfx.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (0)

struct Harness
{
    bool Bgfx = false;
    bool Context = false;
    bool Backend = false;
    bgfx::TextureHandle Target = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle Readback = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle Framebuffer = BGFX_INVALID_HANDLE;
    ImTextureData Rgba, Alpha;

    static void DestroyTexture(ImTextureData& texture)
    {
        if (!texture.BackendUserData) return;
        texture.WantDestroyNextFrame = true;
        texture.UnusedFrames = 1;
        texture.SetStatus(ImTextureStatus_WantDestroy);
        ImGui_ImplBgfx_UpdateTexture(&texture);
    }
    ~Harness()
    {
        if (Backend)
        {
            DestroyTexture(Rgba);
            DestroyTexture(Alpha);
            ImGui_ImplBgfx_Shutdown();
        }
        if (Context) ImGui::DestroyContext();
        if (Bgfx)
        {
            if (bgfx::isValid(Framebuffer)) bgfx::destroy(Framebuffer);
            if (bgfx::isValid(Target)) bgfx::destroy(Target);
            if (bgfx::isValid(Readback)) bgfx::destroy(Readback);
            bgfx::shutdown();
        }
    }
    void Init()
    {
        bgfx::Init init;
        init.type = bgfx::RendererType::Vulkan;
        init.fallback = false;
        init.swapChain.width = init.swapChain.height = 0;
        Bgfx = bgfx::init(init);
        CHECK(Bgfx);
        ImGui::CreateContext(); Context = true;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(128, 128);
        io.DeltaTime = 1.0f / 60.0f;
        Target = bgfx::createTexture2D(128, 128, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        Readback = bgfx::createTexture2D(128, 128, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        CHECK(bgfx::isValid(Target) && bgfx::isValid(Readback));
        Framebuffer = bgfx::createFrameBuffer(1, &Target, false);
        CHECK(bgfx::isValid(Framebuffer));
        ImGui_ImplBgfx_InitInfo info;
        info.ViewId = 1; info.FrameBuffer = Framebuffer;
        Backend = ImGui_ImplBgfx_Init(info);
        CHECK(Backend);
        bgfx::setViewClear(1, BGFX_CLEAR_COLOR, 0x000000ff);
    }
    void FontFrame(float size)
    {
        CHECK(ImGui_ImplBgfx_NewFrame());
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(128, 128));
        ImGui::Begin("Font test");
        ImGui::PushFont(nullptr, size);
        ImGui::TextUnformatted("Dynamic font 123");
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        CHECK(ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData()));
        for (auto* texture : ImGui::GetPlatformIO().Textures)
            CHECK(texture->Status == ImTextureStatus_OK || texture->Status == ImTextureStatus_Destroyed);
        bgfx::frame();
    }
    std::vector<unsigned char> Draw(ImDrawList& list, ImVec2 pos = ImVec2(0, 0), ImVec2 scale = ImVec2(1, 1))
    {
        ImDrawData data;
        data.Valid = true;
        data.DisplayPos = pos;
        data.DisplaySize = ImVec2(128 / scale.x, 128 / scale.y);
        data.FramebufferScale = scale;
        data.AddDrawList(&list);
        CHECK(ImGui_ImplBgfx_RenderDrawData(&data));
        CHECK(ImGui::GetPlatformIO().Renderer_RenderState == nullptr);
        bgfx::TextureRegion dst, src;
        dst.init(Readback); src.init(Target);
        bgfx::blit(2, dst, src);
        bgfx::frame();
        // Read on the next frame so the readback command cannot precede the blit.
        std::vector<unsigned char> pixels(128 * 128 * 4);
        const uint32_t ready = bgfx::read(dst, pixels.data());
        uint32_t frame = bgfx::frame();
        for (unsigned tries = 0; frame < ready && tries < 16; ++tries) frame = bgfx::frame();
        CHECK(frame >= ready);
        return pixels;
    }
};

static void Pixel(const std::vector<unsigned char>& pixels, int x, int y, int r, int g, int b, int a = 255)
{
    const auto* p = pixels.data() + (y * 128 + x) * 4;
    const bool match = std::abs(int(p[0]) - r) <= 3 && std::abs(int(p[1]) - g) <= 3
        && std::abs(int(p[2]) - b) <= 3 && std::abs(int(p[3]) - a) <= 3;
    if (!match) std::fprintf(stderr, "Pixel %d,%d: %u,%u,%u,%u expected %d,%d,%d,%d\n", x, y, p[0], p[1], p[2], p[3], r, g, b, a);
    CHECK(match);
}
static void CustomCallback(const ImDrawList*, const ImDrawCmd* command)
{
    auto* state = static_cast<ImGui_ImplBgfx_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
    CHECK(state && state->Encoder);
    *static_cast<bool*>(command->UserCallbackData) = true;
    state->Encoder->setState(0); // Must not leak into the next ImGui draw.
}

int main()
{
    try
    {
        Harness h;
        h.Init();
        CHECK(ImGui_ImplBgfx_TextureHandle(ImGui_ImplBgfx_TextureID({0})).idx == 0);
        CHECK(!bgfx::isValid(ImGui_ImplBgfx_TextureHandle(ImTextureID_Invalid)));
        CHECK(!bgfx::isValid(ImGui_ImplBgfx_TextureHandle(0)));
        h.FontFrame(16);
        h.FontFrame(29); // Exercise dynamic glyph uploads.

        h.Rgba.Create(ImTextureFormat_RGBA32, 4, 4);
        std::memset(h.Rgba.Pixels, 255, h.Rgba.GetSizeInBytes());
        h.Rgba.SetStatus(ImTextureStatus_WantCreate);
        CHECK(ImGui_ImplBgfx_UpdateTexture(&h.Rgba));
        CHECK(h.Rgba.Status == ImTextureStatus_OK);
        h.Alpha.Create(ImTextureFormat_Alpha8, 4, 4);
        std::memset(h.Alpha.Pixels, 128, h.Alpha.GetSizeInBytes());
        h.Alpha.SetStatus(ImTextureStatus_WantCreate);
        CHECK(ImGui_ImplBgfx_UpdateTexture(&h.Alpha));
        bool callback_called = false;
        auto& platform = ImGui::GetPlatformIO();
        {
            ImDrawList list(ImGui::GetDrawListSharedData()); list._ResetForNewFrame();
            list.PushClipRect(ImVec2(10, 20), ImVec2(74, 84));
            list.AddCallback(CustomCallback, &callback_called);
            list.AddCallback(platform.DrawCallback_SetSamplerNearest, nullptr);
            list.AddImage(h.Rgba.GetTexRef(), ImVec2(10, 20), ImVec2(42, 52), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 0, 0, 255));
            list.PushClipRect(ImVec2(42, 20), ImVec2(58, 36), true);
            list.AddImage(h.Rgba.GetTexRef(), ImVec2(42, 20), ImVec2(74, 52), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 255, 0, 255));
            list.PopClipRect();
            list.AddCallback(platform.DrawCallback_ResetRenderState, nullptr);
            list.AddImage(h.Alpha.GetTexRef(), ImVec2(10, 52), ImVec2(42, 84));
            list.PopClipRect();
            const auto pixels = h.Draw(list, ImVec2(10, 20), ImVec2(2, 2));
            CHECK(callback_called);
            Pixel(pixels, 20, 20, 255, 0, 0);
            Pixel(pixels, 80, 16, 0, 255, 0);
            Pixel(pixels, 112, 48, 0, 0, 0); // Clipped out despite lying inside image geometry.
            Pixel(pixels, 20, 90, 128, 128, 128); // Alpha8 upload and separate alpha blending.
        }
        // Upload a non-contiguous 2x2 region with a 4-pixel source row pitch.
        for (int y = 1; y < 3; ++y)
            for (int x = 1; x < 3; ++x)
            {
                auto* p = static_cast<unsigned char*>(h.Rgba.GetPixelsAt(x, y));
                p[0] = p[1] = 0; p[2] = p[3] = 255;
            }
        h.Rgba.Updates.push_back({1, 1, 2, 2});
        h.Rgba.SetStatus(ImTextureStatus_WantUpdates);
        CHECK(ImGui_ImplBgfx_UpdateTexture(&h.Rgba));
        CHECK(h.Rgba.Status == ImTextureStatus_OK);
        {
            ImDrawList list(ImGui::GetDrawListSharedData()); list._ResetForNewFrame();
            list.PushClipRect(ImVec2(0, 0), ImVec2(128, 128));
            list.AddCallback(platform.DrawCallback_SetSamplerNearest, nullptr);
            list.AddImage(h.Rgba.GetTexRef(), ImVec2(0, 0), ImVec2(128, 128));
            list.PopClipRect();
            auto pixels = h.Draw(list);
            Pixel(pixels, 64, 64, 0, 0, 255);
            Pixel(pixels, 8, 8, 255, 255, 255);
        }
        // More than 64K vertices with 16-bit indices must honor per-command VtxOffset.
        {
            ImDrawList list(ImGui::GetDrawListSharedData()); list._ResetForNewFrame();
            list.Flags |= ImDrawListFlags_AllowVtxOffset;
            list.PushClipRect(ImVec2(0, 0), ImVec2(128, 128));
            for (int i = 0; i < 17000; ++i)
                list.AddImage(h.Rgba.GetTexRef(), ImVec2(-8, -8), ImVec2(-4, -4));
            list.AddImage(h.Rgba.GetTexRef(), ImVec2(16, 16), ImVec2(48, 48), ImVec2(0, 0), ImVec2(0, 0), IM_COL32(255, 0, 0, 255));
            list.PopClipRect();
            CHECK(list.VtxBuffer.Size > 65535);
            if constexpr (sizeof(ImDrawIdx) == 2)
            {
                bool has_offset = false;
                for (const auto& command : list.CmdBuffer) has_offset |= command.VtxOffset > 0;
                CHECK(has_offset);
            }
            auto pixels = h.Draw(list);
            Pixel(pixels, 24, 24, 255, 0, 0);
        }
        // Destruction requests must be processed even with no visible drawing.
        h.Alpha.WantDestroyNextFrame = true;
        h.Alpha.UnusedFrames = 1;
        h.Alpha.SetStatus(ImTextureStatus_WantDestroy);
        ImVector<ImTextureData*> textures; textures.push_back(&h.Alpha);
        ImDrawData empty; empty.Textures = &textures;
        CHECK(ImGui_ImplBgfx_RenderDrawData(&empty));
        CHECK(h.Alpha.Status == ImTextureStatus_Destroyed && h.Alpha.BackendUserData == nullptr);
        CHECK(h.Alpha.GetTexID() == ImTextureID_Invalid);
        ImGui_ImplBgfx_InvalidateDeviceObjects();
        h.FontFrame(24); // Recreate shader/uniform and atlas after invalidation.
        std::puts("PASS: Vulkan offscreen pixels, dynamic fonts, RGBA/Alpha8, partial updates, clipping/DPI, callbacks, VtxOffset, destruction and recreation.");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    return 0;
}
