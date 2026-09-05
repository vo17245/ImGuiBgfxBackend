@echo off
setlocal
rem Usage: BuildBgfx.bat [Release|Debug|RelWithDebInfo|MinSizeRel] [x64|Win32|ARM64]
rem Requires CMake and Visual Studio 2022 with C++ build tools.
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Release"
set "ARCH=%~2"
if not defined ARCH set "ARCH=x64"
if /i "%CONFIG%"=="Release" goto config_ok
if /i "%CONFIG%"=="Debug" goto config_ok
if /i "%CONFIG%"=="RelWithDebInfo" goto config_ok
if /i "%CONFIG%"=="MinSizeRel" goto config_ok
echo ERROR: Unsupported configuration: %CONFIG%
exit /b 1
:config_ok
for %%I in ("%~dp0..") do set "DEPS_DIR=%%~fI"
set "SOURCE_DIR=%DEPS_DIR%\Scripts\BgfxBuild"
rem Use a separate build tree from the previous Repos-based CMake configuration.
set "BUILD_DIR=%DEPS_DIR%\Build\BgfxBuild-%ARCH%"
set "INSTALL_DIR=%DEPS_DIR%\Packages"

set "LOG_DIR=%DEPS_DIR%\Log"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
if not exist "%LOG_DIR%" (
    echo ERROR: Cannot create log directory "%LOG_DIR%".
    exit /b 1
)
set "LOG_FILE=%LOG_DIR%\BuildBgfx-%ARCH%-%CONFIG%.log"
echo Building Bgfx %CONFIG% %ARCH%. Log: "%LOG_FILE%"
call :build > "%LOG_FILE%" 2>&1
set "BUILD_RESULT=%ERRORLEVEL%"
if not "%BUILD_RESULT%"=="0" (
    echo ERROR: Bgfx build failed. See "%LOG_FILE%".
    exit /b %BUILD_RESULT%
)
echo Bgfx %CONFIG% installed to "%INSTALL_DIR%".
exit /b 0

:build
if not exist "%SOURCE_DIR%\CMakeLists.txt" (
   echo ERROR: Bgfx CMake configuration not found at "%SOURCE_DIR%".
    exit /b 1
)

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" -A "%ARCH%" ^
    "-DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%" ^
    -DBGFX_MSVC_STATIC_RUNTIME=ON
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --config "%CONFIG%" --parallel
if errorlevel 1 exit /b 1
cmake --install "%BUILD_DIR%" --config "%CONFIG%"
if errorlevel 1 exit /b 1
echo Bgfx %CONFIG% installed to "%INSTALL_DIR%".
exit /b 0
