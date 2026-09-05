#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_bgfx.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

static int Run(SDL_Window* window, bool smoke_test)
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) return 1;
    void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd)
    {
        std::fprintf(stderr, "SDL HWND unavailable: %s\n", SDL_GetError());
        return 1;
    }
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.fallback = false;
    init.swapChain.nwh = hwnd;
    init.swapChain.width = static_cast<uint32_t>(std::max(width, 1));
    init.swapChain.height = static_cast<uint32_t>(std::max(height, 1));
    init.reset = BGFX_RESET_VSYNC;
    // ImGui/SDL stay on the main (API) thread; bgfx creates its own render thread.
    if (!bgfx::init(init))
    {
        std::fprintf(stderr, "bgfx initialization failed. Run the window demo in an interactive desktop session.\n");
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (smoke_test) io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForOther(window))
    {
        ImGui::DestroyContext();
        bgfx::shutdown();
        return 1;
    }
    if (!ImGui_ImplBgfx_Init())
    {
        std::fprintf(stderr, "ImGui bgfx backend initialization failed.\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        bgfx::shutdown();
        return 1;
    }

    // A caller-owned RGBA checkerboard exercises the backend's user texture API.
    std::array<unsigned char, 16 * 16 * 4> pixels{};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            const bool bright = ((x / 4) + (y / 4)) % 2 == 0;
            const int index = (y * 16 + x) * 4;
            pixels[index] = bright ? 64 : 25;
            pixels[index + 1] = bright ? 205 : 40;
            pixels[index + 2] = bright ? 170 : 60;
            pixels[index + 3] = 255;
        }
    auto image = bgfx::createTexture2D(16, 16, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
    bool done = false, show_demo = true, nearest = true;
    float clear_color[3]{0.06f, 0.09f, 0.14f};
    float font_size = 20.0f;
    char input[128] = "SDL text input";
    int clicks = 0, result = bgfx::isValid(image) ? 0 : 1;
    uint32_t frame = 0;
    auto swap_chain = init.swapChain;
    while (!done && result == 0)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)))
                done = true;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !io.WantCaptureKeyboard)
                done = true;
        }
        if (done) break;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height)) { result = 1; break; }
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) || width <= 0 || height <= 0)
        {
            SDL_Delay(10);
            continue;
        }
        if (swap_chain.width != static_cast<uint32_t>(width) || swap_chain.height != static_cast<uint32_t>(height))
        {
            swap_chain.width = static_cast<uint32_t>(width);
            swap_chain.height = static_cast<uint32_t>(height);
            bgfx::reset(BGFX_RESET_VSYNC, &swap_chain);
        }
        if (!ImGui_ImplBgfx_NewFrame()) { result = 1; break; }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 470), ImGuiCond_FirstUseEver);
        ImGui::Begin("SDL3 + bgfx");
        ImGui::PushFont(nullptr, font_size);
        ImGui::TextUnformatted("Dear ImGui / bgfx renderer");
        ImGui::PopFont();
        ImGui::Text("Backend: %s", bgfx::getRendererName(bgfx::getRendererType()));
        ImGui::Text("%.1f FPS | %d x %d pixels", io.Framerate, width, height);
        ImGui::Separator();
        ImGui::Checkbox("Show ImGui demo", &show_demo);
        ImGui::ColorEdit3("Background", clear_color);
        ImGui::SliderFloat("Font size", &font_size, 14.0f, 40.0f);
        ImGui::InputText("Text", input, IM_ARRAYSIZE(input));
        if (ImGui::Button("Click me")) ++clicks;
        ImGui::SameLine(); ImGui::Text("Clicks: %d", clicks);
        ImGui::Checkbox("Nearest texture sampling", &nearest);
        auto* list = ImGui::GetWindowDrawList();
        auto& platform = ImGui::GetPlatformIO();
        list->AddCallback(nearest ? platform.DrawCallback_SetSamplerNearest : platform.DrawCallback_SetSamplerLinear, nullptr);
        ImGui::Image(ImTextureRef(ImGui_ImplBgfx_TextureID(image)), ImVec2(128, 128));
        list->AddCallback(platform.DrawCallback_ResetRenderState, nullptr);
        ImGui::End();
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);
        ImGui::Render();

        const uint32_t rgba = (static_cast<uint32_t>(clear_color[0] * 255.0f) << 24)
            | (static_cast<uint32_t>(clear_color[1] * 255.0f) << 16)
            | (static_cast<uint32_t>(clear_color[2] * 255.0f) << 8) | 255;
        bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba);
        bgfx::touch(0);
        if (!ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData()))
        {
            std::fprintf(stderr, "ImGui rendering failed (texture upload or transient buffer capacity).\n");
            result = 1;
        }
        bgfx::frame();
        if (smoke_test && ++frame >= 120) done = true;
    }
    if (bgfx::isValid(image)) bgfx::destroy(image);
    ImGui_ImplBgfx_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    bgfx::shutdown();
    return result;
}

int main(int argc, char** argv)
{
    const bool smoke_test = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
    if (argc != 1 && !smoke_test)
    {
        std::fprintf(stderr, "Usage: ImGuiBgfx.exe [--smoke-test]\n");
        return 1;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    auto* window = SDL_CreateWindow("ImGui + SDL3 + bgfx", 1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    const int result = Run(window, smoke_test);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
