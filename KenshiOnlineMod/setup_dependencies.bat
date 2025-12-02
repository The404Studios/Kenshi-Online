@echo off
setlocal enabledelayedexpansion

echo =====================================================
echo   KenshiOnlineMod Dependency Setup
echo =====================================================
echo.

:: Check if git is available
where git >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: Git is not installed or not in PATH.
    echo Please install Git from https://git-scm.com/
    pause
    exit /b 1
)

:: Create vendor directory
if not exist "vendor" (
    echo Creating vendor directory...
    mkdir vendor
)

cd vendor

:: Clone or update ImGui
echo.
echo [1/2] Setting up ImGui...
if exist "imgui" (
    echo ImGui already exists, updating...
    cd imgui
    git fetch origin
    git checkout v1.90.1
    cd ..
) else (
    echo Cloning ImGui v1.90.1...
    git clone --depth 1 --branch v1.90.1 https://github.com/ocornut/imgui.git imgui
)

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to setup ImGui
    pause
    exit /b 1
)

:: Clone or update MinHook
echo.
echo [2/2] Setting up MinHook...
if exist "minhook" (
    echo MinHook already exists, updating...
    cd minhook
    git fetch origin
    git checkout v1.3.3
    cd ..
) else (
    echo Cloning MinHook v1.3.3...
    git clone --depth 1 --branch v1.3.3 https://github.com/TsudaKageyu/minhook.git minhook
)

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to setup MinHook
    pause
    exit /b 1
)

cd ..

echo.
echo =====================================================
echo   Setup Complete!
echo =====================================================
echo.
echo Dependencies have been installed to the vendor folder.
echo You can now open KenshiOnlineMod.sln in Visual Studio.
echo.
echo Build configurations available:
echo   - Debug   x86/x64
echo   - Release x86/x64
echo.
echo Note: Kenshi uses x64, so build with x64 configuration.
echo.

pause
