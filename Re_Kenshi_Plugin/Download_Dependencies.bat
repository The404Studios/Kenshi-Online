@echo off
setlocal enabledelayedexpansion

:: ============================================================================
::  Download Third-Party Dependencies for Re_Kenshi Plugin
:: ============================================================================

color 0B
cls

echo.
echo  ╔═══════════════════════════════════════════════════════════╗
echo  ║                                                           ║
echo  ║         Download C++ Dependencies                         ║
echo  ║                                                           ║
echo  ╚═══════════════════════════════════════════════════════════╝
echo.

cd /d "%~dp0"

:: Check for PowerShell
where powershell >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo  ❌ ERROR: PowerShell not found!
    echo.
    echo  Please download dependencies manually.
    echo  See vendor/README.md for instructions.
    echo.
    pause
    exit /b 1
)

echo  Downloading dependencies...
echo.

:: ============================================================================
:: Download nlohmann/json
:: ============================================================================

echo  [1/1] Downloading nlohmann/json...
echo  ───────────────────────────────────────────────────────────

set JSON_URL=https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
set JSON_PATH=vendor\nlohmann\json.hpp

if exist "%JSON_PATH%" (
    echo  ⚠  File exists: %JSON_PATH%
    echo.
    set /p OVERWRITE="    Overwrite? (y/n): "
    if /i not "!OVERWRITE!"=="y" (
        echo  Skipping...
        goto :done
    )
    del "%JSON_PATH%"
)

echo  Downloading from: %JSON_URL%
echo  Saving to: %JSON_PATH%
echo.

powershell -Command "& {[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%JSON_URL%' -OutFile '%JSON_PATH%'}"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  ❌ Download failed!
    echo.
    echo  Please download manually from:
    echo  %JSON_URL%
    echo.
    echo  Save to:
    echo  %JSON_PATH%
    echo.
    pause
    exit /b 1
)

echo.
echo  ✓ Downloaded successfully!
echo.

:: ============================================================================
:: Verify
:: ============================================================================

:done
echo  ╔═══════════════════════════════════════════════════════════╗
echo  ║              Verifying Downloads                          ║
echo  ╚═══════════════════════════════════════════════════════════╝
echo.

if exist "%JSON_PATH%" (
    for %%F in ("%JSON_PATH%") do set SIZE=%%~zF
    if !SIZE! GTR 100000 (
        echo  ✓ nlohmann/json: OK (!SIZE! bytes)
    ) else (
        echo  ⚠  nlohmann/json: File too small (stub only)
        echo     Please re-run or download manually
    )
) else (
    echo  ❌ nlohmann/json: NOT FOUND
)

echo.
echo  ╔═══════════════════════════════════════════════════════════╗
echo  ║              Download Complete!                           ║
echo  ╚═══════════════════════════════════════════════════════════╝
echo.
echo  You can now build the C++ plugin with:
echo.
echo    mkdir build
echo    cd build
echo    cmake .. -G "Visual Studio 17 2022" -A x64
echo    cmake --build . --config Release
echo.
pause
