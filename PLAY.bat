@echo off
setlocal enabledelayedexpansion

:: ============================================================================
::  KENSHI ONLINE - One-Click Launcher
:: ============================================================================

color 0B
cls

echo.
echo  ╔═══════════════════════════════════════════════════════════════╗
echo  ║                                                               ║
echo  ║          KENSHI ONLINE - Unified Launcher v2.0                ║
echo  ║                                                               ║
echo  ╚═══════════════════════════════════════════════════════════════╝
echo.

:: Check if launcher exists
if exist "bin\Release\KenshiOnline.exe" (
    echo  ✓ Launcher found!
    echo.
    echo  Starting Kenshi Online...
    echo.
    cd bin\Release
    start "" "KenshiOnline.exe"
    exit /b 0
)

:: Build if not found
echo  ⚠ Launcher not built yet. Building now...
echo.
echo  This will take a minute on first run.
echo.

:: Check for .NET SDK
where dotnet >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo  ❌ ERROR: .NET SDK not found!
    echo.
    echo  Please install .NET 8.0 SDK from:
    echo  https://dotnet.microsoft.com/download
    echo.
    pause
    exit /b 1
)

echo  Building unified launcher...
echo.

cd KenshiOnline.Launcher

:: Build as single-file executable
dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true /p:PublishTrimmed=false

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  ❌ Build failed!
    echo.
    pause
    exit /b 1
)

cd ..

echo.
echo  ✓ Build complete!
echo.
echo  Starting Kenshi Online...
echo.

cd bin\Release
start "" "KenshiOnline.exe"

echo  Launcher started!
echo.
