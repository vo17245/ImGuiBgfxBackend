@echo off
setlocal
rem Usage: Build.bat [Release|Debug] [x64|Win32|ARM64]
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Release"
set "ARCH=%~2"
if not defined ARCH set "ARCH=x64"
if /i "%CONFIG%"=="Release" goto config_ok
if /i "%CONFIG%"=="Debug" goto config_ok
echo ERROR: Use Release or Debug.
exit /b 1
:config_ok
for %%I in ("%~dp0.") do set "APP_DIR=%%~fI"
for %%I in ("%~dp0..\Dependencies") do set "DEPS_DIR=%%~fI"
set "BUILD_DIR=%APP_DIR%\Build\%ARCH%"
set "LOG_DIR=%APP_DIR%\Log"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
if not exist "%LOG_DIR%" exit /b 1
set "LOG_FILE=%LOG_DIR%\Build-%ARCH%-%CONFIG%.log"
echo Building ImGuiBgfx %CONFIG% %ARCH%. Log: "%LOG_FILE%"
call :build > "%LOG_FILE%" 2>&1
set "BUILD_RESULT=%ERRORLEVEL%"
if not "%BUILD_RESULT%"=="0" (
    echo ERROR: Build failed. See "%LOG_FILE%".
    exit /b %BUILD_RESULT%
)
echo Executable: "%BUILD_DIR%\%CONFIG%\ImGuiBgfx.exe"
exit /b 0

:build
rem Reconfigure dependencies for the selected architecture and CRT configuration.
call "%DEPS_DIR%\Scripts\BuildSDL.bat" "%CONFIG%" "%ARCH%"
if errorlevel 1 exit /b 1
call "%DEPS_DIR%\Scripts\BuildBgfx.bat" "%CONFIG%" "%ARCH%"
if errorlevel 1 exit /b 1
cmake -S "%APP_DIR%" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" -A "%ARCH%" ^
    "-DCMAKE_PREFIX_PATH=%DEPS_DIR%\Packages"
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --config "%CONFIG%" --parallel
if errorlevel 1 exit /b 1
exit /b 0
