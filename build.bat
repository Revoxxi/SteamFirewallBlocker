@echo off
setlocal

set "GENERATOR=Ninja"

call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 >nul 2>&1
if errorlevel 1 (
    echo Failed to initialize Visual Studio build environment.
    exit /b 1
)

cd /d "%~dp0"

where cl >nul 2>&1
if errorlevel 1 (
    echo MSVC compiler ^(cl.exe^) not found.
    echo Install "Desktop development with C++" in Visual Studio Installer.
    exit /b 1
)

if not exist "%ProgramFiles(x86)%\Windows Kits\10\Include" (
    echo Windows 10/11 SDK not found.
    echo.
    echo Open Visual Studio Installer, modify Build Tools 2022, and install:
    echo   - Windows 10 SDK or Windows 11 SDK
    echo.
    echo Then run build.bat again.
    exit /b 1
)

if exist build\CMakeCache.txt (
    findstr /B /C:"CMAKE_GENERATOR:INTERNAL=%GENERATOR%" build\CMakeCache.txt >nul
    if errorlevel 1 (
        echo Removing incompatible CMake cache ^(generator changed^)...
        rmdir /s /q build
    )
)

if not exist build mkdir build

cmake -B build -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build build
if errorlevel 1 exit /b 1

echo.
echo Build complete: build\SteamFirewallBlocker.exe
