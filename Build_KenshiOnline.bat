@echo off
echo ╔════════════════════════════════════════════════════════╗
echo ║          Kenshi Online Build Script v2.0              ║
echo ╚════════════════════════════════════════════════════════╝
echo.

REM Check for MSBuild
where /q msbuild
if ERRORLEVEL 1 (
    echo [ERROR] MSBuild not found! Please install Visual Studio 2022 or Build Tools
    echo Download from: https://visualstudio.microsoft.com/downloads/
    pause
    exit /b 1
)

REM Check for .NET SDK
where /q dotnet
if ERRORLEVEL 1 (
    echo [ERROR] .NET SDK not found! Please install .NET 8.0 SDK
    echo Download from: https://dotnet.microsoft.com/download
    pause
    exit /b 1
)

echo [1/3] Building C# Core Library...
echo ═══════════════════════════════════════════════════════
cd KenshiOnline.Core
dotnet build -c Release
if ERRORLEVEL 1 (
    echo [ERROR] Core library build failed!
    cd ..
    pause
    exit /b 1
)
cd ..
echo ✓ Core library built successfully
echo.

echo [2/3] Building C# Server...
echo ═══════════════════════════════════════════════════════
cd KenshiOnline.Server
dotnet build -c Release
if ERRORLEVEL 1 (
    echo [ERROR] Server build failed!
    cd ..
    pause
    exit /b 1
)
cd ..
echo ✓ Server built successfully
echo.

echo [3/3] Building C# Client Service...
echo ═══════════════════════════════════════════════════════
cd KenshiOnline.ClientService
dotnet build -c Release
if ERRORLEVEL 1 (
    echo [ERROR] Client service build failed!
    cd ..
    pause
    exit /b 1
)
cd ..
echo ✓ Client service built successfully
echo.

echo ╔════════════════════════════════════════════════════════╗
echo ║                  Build Successful!                     ║
echo ╚════════════════════════════════════════════════════════╝
echo.
echo Next steps:
echo   1. Inject Re_Kenshi_Plugin.dll into kenshi_x64.exe
echo   2. Run Host_Server.bat to start the server
echo   3. Run Join_Server.bat to connect as a client
echo.
pause
