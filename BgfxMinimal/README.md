# BgfxMinimal

Windows 上的 C++23 / SDL3 / bgfx 最小窗口示例，默认使用 Direct3D11。

## 构建和运行

需要 Visual Studio 2022 C++ 工具链、Windows SDK、CMake 3.25+ 和 Git。
在项目根目录运行：

```bat
BgfxMinimal\Build.bat
BgfxMinimal\Build\x64\Release\BgfxMinimal.exe
```

Debug 配置：

```bat
BgfxMinimal\Build.bat Debug
BgfxMinimal\Build\x64\Debug\BgfxMinimal.exe
```

`Build.bat [Release|Debug] [x64|Win32|ARM64]` 默认 Release/x64。
脚本先调用 Dependencies/Scripts 下的 SDL 和 bgfx 构建脚本，确保
Dependencies/Packages 中的库匹配配置和架构，再构建示例。当前已验证 x64 构建。
Debug 使用 /MTd，Release 使用 /MT。CMake 要求 C++23；本机 MSVC/CMake
组合将其映射为 `/std:c++latest`。

- 示例构建目录：`BgfxMinimal/Build/<架构>`。
- 示例构建日志：`BgfxMinimal/Log/Build-<架构>-<配置>.log`。
- 依赖构建日志：`Dependencies/Log`。
- 运行无需 SDL/bgfx DLL 或外部 shader 文件。

也可以在依赖安装完成后直接使用 CMake 配置本目录。

## 界面与操作

深色背景上显示渲染后端、像素尺寸、帧计数和一条移动的彩色横条。

- F1：切换 bgfx 性能统计界面。
- Space：暂停/继续横条动画。
- C：切换横条颜色。
- Esc 或关闭窗口：退出。
- 拖动窗口边缘：调整尺寸；最小化时暂停提交帧，恢复后继续。

## 线程和当前 API

线程结构参考 [helloworld_mt.cpp](https://github.com/jpcy/bgfx-minimal-example/blob/master/helloworld_mt.cpp)，
重新实现为 SDL3 事件处理和 C++ 标准线程：

- 主线程创建 SDL 窗口、处理事件、获取像素尺寸并调用 `bgfx::renderFrame(16)`。
- 工作线程调用 `bgfx::init`、更新界面、提交 `bgfx::frame` 并执行 `bgfx::shutdown`。
- mutex 保护窗口状态，atomic 通知工作线程结束。退出时持续处理渲染，待 shutdown
  完成并 join 工作线程后才销毁 SDL 窗口。

SDL HWND 通过 `SDL_GetWindowProperties` / `SDL_PROP_WINDOW_WIN32_HWND_POINTER`
获取。当前 bgfx 使用 `init.swapChain.nwh/width/height` 和 `init.reset`，
缩放使用 `bgfx::reset(flags, &swapChain)`，不再使用旧示例中的
`PlatformData::nwh` 或 `Init::resolution`。

## 验证

x64 Debug 和 Release 均已通过完整 Build.bat 构建，无编译警告。
可在交互式 Windows 桌面运行以下测试，自动执行窗口缩放、最小化、恢复、快捷键和退出：

```bat
BgfxMinimal\Build\x64\Debug\BgfxMinimal.exe --smoke-test
```

测试约运行 3 秒，成功返回 0，初始化失败或未收到预期事件返回非零。

本次自动执行环境位于 Session 0，D3D11 创建交换链返回 `0x887A0022`
(`DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`)，因此尚未验证实际显示和完整交互测试。
程序已验证能在初始化失败时输出错误并正常返回 1。
[Microsoft 文档](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createswapchain)
说明了 Session 0 中的交换链限制。
