@echo off
setlocal
rem Usage: BuildSDL.bat [Release|Debug|RelWithDebInfo|MinSizeRel] [x64|Win32|ARM64]
rem Requires CMake, Git and Visual Studio 2022 with C++ build tools.
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
set "SOURCE_DIR=%DEPS_DIR%\Repos\SDL"
set "BUILD_DIR=%DEPS_DIR%\Build\SDL-%ARCH%"
set "INSTALL_DIR=%DEPS_DIR%\Packages"
set "PATCH_FILE=%DEPS_DIR%\Patches\SDL-static-crt.patch"

set "LOG_DIR=%DEPS_DIR%\Log"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
if not exist "%LOG_DIR%" (
    echo ERROR: Cannot create log directory "%LOG_DIR%".
    exit /b 1
)
set "LOG_FILE=%LOG_DIR%\BuildSDL-%ARCH%-%CONFIG%.log"
echo Building SDL %CONFIG% %ARCH%. Log: "%LOG_FILE%"
call :build > "%LOG_FILE%" 2>&1
set "BUILD_RESULT=%ERRORLEVEL%"
if not "%BUILD_RESULT%"=="0" (
    echo ERROR: SDL build failed. See "%LOG_FILE%".
    exit /b %BUILD_RESULT%
)
echo SDL %CONFIG% installed to "%INSTALL_DIR%".
exit /b 0

:build
if not exist "%SOURCE_DIR%\CMakeLists.txt" (
   echo ERROR: SDL sources not found at "%SOURCE_DIR%".
    exit /b 1
)
rem Apply the local patch only if it has not already been applied.
git -C "%SOURCE_DIR%" apply --reverse --check "%PATCH_FILE%" >nul 2>&1
if not errorlevel 1 goto patched
git -C "%SOURCE_DIR%" apply --check "%PATCH_FILE%"
if errorlevel 1 exit /b 1
git -C "%SOURCE_DIR%" apply "%PATCH_FILE%"
if errorlevel 1 exit /b 1
:patched

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" -A "%ARCH%" ^
    "-DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%" ^
    -DSDL_MSVC_STATIC_RUNTIME=ON -DSDL_LIBC=ON ^
    -DSDL_STATIC=ON -DSDL_SHARED=OFF -DCMAKE_DEBUG_POSTFIX=d ^
    -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF ^
    -DSDL_INSTALL=ON
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --config "%CONFIG%" --parallel
if errorlevel 1 exit /b 1
cmake --install "%BUILD_DIR%" --config "%CONFIG%"
if errorlevel 1 exit /b 1
echo SDL %CONFIG% installed to "%INSTALL_DIR%".
exit /b 0
