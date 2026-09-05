#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

struct WindowState
{
    int width = 1024;
    int height = 720;
    bool minimized = false;
    bool quit = false;
    bool stats = false;
    bool paused = false;
    unsigned palette = 0;
};

struct SharedState
{
    std::mutex mutex;
    WindowState window;
    std::atomic<bool> done = false;
    int result = 0; // Read by the main thread only after join().
};

// All bgfx API calls except renderFrame belong to this worker.
void runApiThread(SharedState& shared, void* nativeWindow)
{
    WindowState state;
    {
        const std::lock_guard lock(shared.mutex);
        state = shared.window;
    }

    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.fallback = false;
    init.swapChain.nwh = nativeWindow;
    init.swapChain.width = static_cast<uint32_t>(std::max(state.width, 1));
    init.swapChain.height = static_cast<uint32_t>(std::max(state.height, 1));
    init.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init))
    {
        std::fprintf(stderr, "bgfx Direct3D11 initialization failed.\n");
        shared.result = 1;
        shared.done.store(true);
        return;
    }

    auto swapChain = init.swapChain;
    constexpr std::array<uint32_t, 3> accents{0x40c9b0ff, 0x739cffff, 0xe9ad63ff};
    double animation = 0.0;
    auto previous = std::chrono::steady_clock::now();
    uint32_t frames = 0;
    while (true)
    {
        {
            const std::lock_guard lock(shared.mutex);
            state = shared.window;
        }
        if (state.quit)
            break;

        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (state.minimized || state.width <= 0 || state.height <= 0)
        {
            std::this_thread::sleep_for(16ms);
            continue;
        }
        if (!state.paused)
            animation += delta;

        const auto width = static_cast<uint16_t>(std::clamp(state.width, 1, 65535));
        const auto height = static_cast<uint16_t>(std::clamp(state.height, 1, 65535));
        if (swapChain.width != width || swapChain.height != height)
        {
            swapChain.width = width;
            swapChain.height = height;
            bgfx::reset(BGFX_RESET_VSYNC, &swapChain);
        }

        bgfx::setViewRect(0, 0, 0, width, height);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x111923ff);
        bgfx::touch(0);

        // Clear a second view to draw a moving accent strip without custom shaders.
        const auto stripWidth = static_cast<uint16_t>(std::max(1, width / 3));
        const double position = (std::sin(animation * 1.2) + 1.0) * 0.5;
        const auto stripX = static_cast<uint16_t>(position * (width - stripWidth));
        const auto stripY = static_cast<uint16_t>(height * 3 / 4);
        const auto stripHeight = static_cast<uint16_t>(std::min(6, height - stripY));
        bgfx::setViewRect(1, stripX, stripY, stripWidth, stripHeight);
        bgfx::setViewClear(1, BGFX_CLEAR_COLOR, accents[state.palette % accents.size()]);
        bgfx::touch(1);

        bgfx::setDebug(state.stats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT);
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(3, 2, 0x0b, "B G F X   M I N I M A L");
        bgfx::dbgTextPrintf(3, 4, 0x0f, "SDL3 window  /  bgfx renderer  /  C++23");
        bgfx::dbgTextPrintf(3, 6, 0x08, "----------------------------------------------------");
        bgfx::dbgTextPrintf(3, 8, 0x07, "Renderer    %s", bgfx::getRendererName(bgfx::getRendererType()));
        bgfx::dbgTextPrintf(3, 10, 0x07, "Framebuffer %u x %u pixels", unsigned(width), unsigned(height));
        bgfx::dbgTextPrintf(3, 12, 0x07, "Animation   %s", state.paused ? "paused" : "running");
        bgfx::dbgTextPrintf(3, 14, 0x07, "Frame       %u", ++frames);
        bgfx::dbgTextPrintf(3, 17, 0x0b, "F1     Performance statistics");
        bgfx::dbgTextPrintf(3, 19, 0x07, "SPACE  Pause animation     C  Change color");
        bgfx::dbgTextPrintf(3, 21, 0x07, "ESC    Exit               Drag edges to resize");
        bgfx::frame();
    }
    // The main thread keeps pumping renderFrame until shutdown has completed.
    bgfx::shutdown();
    std::printf("bgfx shutdown completed after %u frames.\n", frames);
    shared.done.store(true);
}

int main(int argc, char** argv)
{
    bool smokeTest = false;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke-test")
        smokeTest = true;
    else if (argc != 1)
    {
        std::fprintf(stderr, "Usage: BgfxMinimal.exe [--smoke-test]\n");
        return 1;
    }

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("BgfxMinimal | SDL3 + bgfx | C++23", 1024, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    void* nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!nativeWindow)
    {
        std::fprintf(stderr, "SDL did not provide a Win32 HWND: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SharedState shared;
    SDL_GetWindowSizeInPixels(window, &shared.window.width, &shared.window.height);
    // Match helloworld_mt: the SDL/main thread also owns the bgfx render thread.
    bgfx::renderFrame();
    std::thread apiThread;
    try
    {
        apiThread = std::thread(runApiThread, std::ref(shared), nativeWindow);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Cannot create bgfx API thread: %s\n", error.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    unsigned testStep = 0;
    bool sawResize = false;
    bool sawMinimize = false;
    bool sawRestore = false;
    unsigned keyEvents = 0;
    while (!shared.done.load())
    {
        // Exercise the actual SDL event path, including shutdown, in an opt-in smoke test.
        if (smokeTest)
        {
            const auto elapsed = std::chrono::steady_clock::now() - started;
            if (testStep == 0 && elapsed > 500ms)
            {
                SDL_SetWindowSize(window, 800, 600);
                ++testStep;
            }
            else if (testStep == 1 && elapsed > 1000ms)
            {
                SDL_MinimizeWindow(window);
                ++testStep;
            }
            else if (testStep == 2 && elapsed > 1400ms)
            {
                SDL_RestoreWindow(window);
                ++testStep;
            }
            else if (testStep >= 3 && testStep <= 6 && elapsed > std::chrono::milliseconds(1400 + (testStep - 2) * 250))
            {
                constexpr std::array<SDL_Keycode, 4> keys{SDLK_F1, SDLK_F1, SDLK_SPACE, SDLK_C};
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.windowID = SDL_GetWindowID(window);
                event.key.key = keys[testStep - 3];
                SDL_PushEvent(&event);
                ++testStep;
            }
            else if (testStep == 7 && elapsed > 3000ms)
            {
                SDL_Event event{};
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
                ++testStep;
            }
        }

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            const std::lock_guard lock(shared.mutex);
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                shared.window.quit = true;
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                shared.window.minimized = true;
                sawMinimize = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                shared.window.minimized = false;
                sawRestore = true;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                sawResize = true;
                break; // Pixel dimensions are queried on the SDL thread below.
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat)
                {
                    ++keyEvents;
                    switch (event.key.key)
                    {
                    case SDLK_ESCAPE: shared.window.quit = true; break;
                    case SDLK_F1: shared.window.stats = !shared.window.stats; break;
                    case SDLK_SPACE: shared.window.paused = !shared.window.paused; break;
                    case SDLK_C: ++shared.window.palette; break;
                    default: break;
                    }
                }
                break;
            default: break;
            }
        }
        int width = 0;
        int height = 0;
        if (SDL_GetWindowSizeInPixels(window, &width, &height))
        {
            const std::lock_guard lock(shared.mutex);
            shared.window.width = width;
            shared.window.height = height;
        }
        // A timeout lets SDL keep processing events while minimized or during startup.
        if (bgfx::renderFrame(16) == bgfx::RenderFrame::NoContext)
            SDL_Delay(1);
    }

    apiThread.join();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (smokeTest)
    {
        const bool passed = sawResize && sawMinimize && sawRestore && keyEvents >= 4 && testStep == 8;
        std::printf("SDL smoke test: resize=%d minimize=%d restore=%d keys=%u completed=%d\n",
            sawResize, sawMinimize, sawRestore, keyEvents, testStep == 8);
        if (!passed)
            return 1;
    }
    return shared.result;
}
