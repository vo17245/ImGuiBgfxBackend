# ImGuiBgfx

Dear ImGui 1.93.0 WIP（`IMGUI_VERSION_NUM=19295`），源码来源：
https://github.com/ocornut/imgui.git

SDL3 作为平台后端，bgfx 作为渲染后端。示例使用 C++23、Windows/MSVC、
静态 SDL/bgfx 和静态 CRT（Release `/MT`、Debug `/MTd`）。

## 构建和运行

需要 Visual Studio 2022 C++ 工具链、Windows SDK、CMake 3.25+ 和 Git。
从项目根目录运行：

```bat
ImGuiBgfx\Build.bat
ImGuiBgfx\Build\x64\Release\ImGuiBgfx.exe
```

Debug：

```bat
ImGuiBgfx\Build.bat Debug
ImGuiBgfx\Build\x64\Debug\ImGuiBgfx.exe
```

`Build.bat [Release|Debug] [x64|Win32|ARM64]` 默认 Release/x64。
脚本先调用 `Dependencies/Scripts/BuildSDL.bat` 和 `BuildBgfx.bat`，
再通过 `Dependencies/Packages` 中的 CMake targets 构建示例。
当前已验证 x64；其他架构需要对应的 Visual Studio 工具链。

- 编译产物：`ImGuiBgfx/Build/<架构>/<配置>`。
- 项目日志：`ImGuiBgfx/Log`，依赖日志：`Dependencies/Log`。
- 界面包含 ImGui Demo、输入框、按钮、字体大小、背景颜色和用户纹理采样切换。
- 支持窗口缩放、高 DPI 像素尺寸、最小化恢复和退出。
- `--smoke-test` 参数会在提交 120 帧后退出，并禁用 imgui.ini 写入。

## 后端接入

核心文件为 `Source/ImGui/imgui_impl_bgfx.h/.cpp`，现有 SDL3 平台后端保持原样。
CMake 的 `imgui_bgfx` 静态库目标包含渲染后端，公开依赖 `imgui` 和 `bgfx::bgfx`。

```cpp
// 在 SDL 主线程上创建窗口并初始化 bgfx。
// 当前 bgfx 的 HWND、像素尺寸位于 Init::swapChain，垂直同步位于 Init::reset。
bgfx::Init init;
init.type = bgfx::RendererType::Direct3D11;
init.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
    SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
init.swapChain.width = pixel_width;
init.swapChain.height = pixel_height;
init.reset = BGFX_RESET_VSYNC;
// 检查 bgfx::init(init) 的返回值，再创建 ImGui context。

ImGui::CreateContext();
ImGui_ImplSDL3_InitForOther(window);
ImGui_ImplBgfx_Init(); // 默认 view 255，必须为 UI 独占；检查初始化返回值。

// 每帧：SDL_PollEvent -> ImGui_ImplSDL3_ProcessEvent
ImGui_ImplBgfx_NewFrame(); // false 表示设备资源创建失败。
ImGui_ImplSDL3_NewFrame();
ImGui::NewFrame();
// 创建界面……
ImGui::Render();
ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData()); // 检查 bool 返回值。
bgfx::frame();

// 退出顺序：
ImGui_ImplBgfx_Shutdown();
ImGui_ImplSDL3_Shutdown();
ImGui::DestroyContext();
bgfx::shutdown();
// 最后销毁 SDL 窗口并 SDL_Quit。
```

完整初始化和失败路径见 `Source/main.cpp`。示例参照 `BgfxMinimal` 的 HWND、
交换链及像素尺寸处理，使用 `bgfx::reset(flags, &swap_chain)` 响应缩放。
这里让 SDL/ImGui/bgfx API 调用都留在主线程，由 bgfx 创建内部渲染线程，
不在工作线程调用 SDL 平台后端，也不手动调用 `bgfx::renderFrame()`。

## 渲染约定

- 支持 `ImGuiBackendFlags_RendererHasTextures`：RGBA32/Alpha8 纹理创建、
  局部更新、销毁和动态字体 atlas。Alpha8 在上传时扩展为白色 RGB 加 alpha。
  创建可更新纹理后再上传初始像素，避免 bgfx 将其标记为 immutable。
- 支持 `RendererHasVtxOffset`：16 位索引下超过 64K 顶点的网格，
  以及 32 位 ImDrawIdx、IdxOffset、DisplayPos、FramebufferScale 和裁剪。
- 按 Sequential view 模式保持 ImGui 绘制顺序，使用直通 alpha 混合并保留目标 alpha。
- 支持标准 ResetRenderState、SetSamplerLinear、SetSamplerNearest 和自定义回调。
  回调期间可通过 `GetPlatformIO().Renderer_RenderState` 访问
  `ImGui_ImplBgfx_RenderState`。修改 view 等全局状态后应插入 ResetRenderState 回调。
- 用户纹理用 `ImGui_ImplBgfx_TextureID(handle)` 转成 ImTextureID，再包装为 ImTextureRef。
  仅支持普通单采样 2D 颜色纹理，纹理归调用者所有，后端不会代为销毁。
  ID 编码为 `handle.idx + 1`，不要直接把 handle.idx 强转为 ImTextureID。
- UI 使用独立 view，默认不清屏，不调用 `bgfx::frame()`。
  初始化参数可设置 ViewId/FrameBuffer，也可调用 SetFrameBuffer 切换离屏目标。
  窗口示例用 view 0 清背景、view 255 绘制 UI。
- 顶点/索引使用瞬时缓冲区，内存由 bgfx 管理。
  如果整帧 UI 无法一次分配，RenderDrawData 返回 false，不绘制残缺帧；
  大型 UI 可在 bgfx::Init::limits 中增加瞬时缓冲容量。
- InvalidateDeviceObjects 释放后端 shader/uniform 和独占的字体纹理，
  下一次 NewFrame 及纹理请求会重建这些资源。
- 未实现多 OS 窗口 viewport，不设置相应 BackendFlags。
  目前不支持 Noop：当前 bgfx 的 embedded Noop shader 版本不兼容。

Shader 使用匹配 bgfx 仓库的 `examples/common/imgui/vs_ocornut_imgui.bin.h`
和 `fs_ocornut_imgui.bin.h`，由 CMake 提供私有 include 路径；程序运行时
无需 shader 文件或 shaderc。升级 bgfx 时，库、头文件和 shader 应来自同一版本。
这些 shader 使用 bgfx 仓库中的 LICENSE；本项目未复制或修改其源码。
如果单独复用后端，需要提供上述两个 shader 头文件及 bgfx/bx 头文件和库。

## 验证

```bat
ctest --test-dir ImGuiBgfx\Build\x64 -C Debug --output-on-failure
ctest --test-dir ImGuiBgfx\Build\x64 -C Release --output-on-failure
```

`Tests/backend_tests.cpp` 创建真实 Vulkan 离屏设备，渲染到 RGBA8 目标并读回
像素验证。测试需要支持 Vulkan 的驱动，不需要 SDL 窗口或桌面交换链。
测试覆盖动态字体、RGBA/Alpha8、带源行距的局部更新、透明混合、非零 DisplayPos、
2 倍 FramebufferScale、裁剪、回调、大于 64K 顶点、空帧纹理销毁与设备资源重建。

x64 Debug、Release 均已编译并通过离屏测试；另一个使用 32 位索引和
`IMGUI_USE_BGRA_PACKED_COLOR` 的 Debug 构建也通过相同像素测试。

当前自动执行环境为 Windows Session 0。D3D11 即使无窗口仍调用
MakeWindowAssociation，并返回 `0x887A0022`；因此桌面示例的实际显示、
鼠标键盘和窗口缩放仍需在交互式桌面运行确认。此限制不影响已通过的
Vulkan 离屏后端测试，也没有通过修改 bgfx 断言来绕过。
