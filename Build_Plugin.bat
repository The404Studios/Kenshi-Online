@echo off
setlocal enabledelayedexpansion

:: ============================================================================
::  Build Re_Kenshi C++ Plugin
:: ============================================================================

color 0E
cls

echo.
echo  ╔═══════════════════════════════════════════════════════════════╗
echo  ║                                                               ║
echo  ║          Build Re_Kenshi C++ Plugin                           ║
echo  ║                                                               ║
echo  ╚═══════════════════════════════════════════════════════════════╝
echo.

cd /d "%~dp0"

:: ============================================================================
:: Step 1: Check Dependencies
:: ============================================================================

echo  [1/5] Checking dependencies...
echo  ═══════════════════════════════════════════════════════════════
echo.

:: Check for CMake
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo  ❌ ERROR: CMake not found!
    echo.
    echo  Please install CMake from:
    echo  https://cmake.org/download/
    echo.
    pause
    exit /b 1
)

echo  ✓ CMake found
echo.

:: Check for Visual Studio
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo  ✓ Visual Studio C++ compiler found
) else (
    echo  ⚠  Visual Studio C++ compiler not in PATH
    echo     Build may fail if Visual Studio is not installed
)

echo.

:: ============================================================================
:: Step 2: Download Third-Party Libraries
:: ============================================================================

echo  [2/5] Checking third-party libraries...
echo  ═══════════════════════════════════════════════════════════════
echo.

set JSON_FILE=Re_Kenshi_Plugin\vendor\nlohmann\json.hpp

if exist "%JSON_FILE%" (
    for %%F in ("%JSON_FILE%") do set SIZE=%%~zF
    if !SIZE! GTR 100000 (
        echo  ✓ nlohmann/json found (!SIZE! bytes)
    ) else (
        echo  ⚠  nlohmann/json is stub only (too small)
        echo.
        set /p DOWNLOAD="Download now? (y/n): "
        if /i "!DOWNLOAD!"=="y" (
            call Re_Kenshi_Plugin\Download_Dependencies.bat
        )
    )
) else (
    echo  ⚠  nlohmann/json NOT found
    echo.
    set /p DOWNLOAD="Download now? (y/n): "
    if /i "!DOWNLOAD!"=="y" (
        call Re_Kenshi_Plugin\Download_Dependencies.bat
    )
)

echo.

:: ============================================================================
:: Step 3: Configure CMake
:: ============================================================================

echo  [3/5] Configuring CMake...
echo  ═══════════════════════════════════════════════════════════════
echo.

cd Re_Kenshi_Plugin

if not exist build mkdir build
cd build

echo  Running CMake configuration...
echo.

cmake .. -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  ❌ CMake configuration failed!
    echo.
    echo  Try:
    echo    1. Install Visual Studio 2022 with C++ support
    echo    2. Or use a different generator:
    echo       cmake .. -G "Visual Studio 16 2019" -A x64
    echo.
    pause
    exit /b 1
)

echo.
echo  ✓ CMake configuration successful
echo.

:: ============================================================================
:: Step 4: Build
:: ============================================================================

echo  [4/5] Building plugin...
echo  ═══════════════════════════════════════════════════════════════
echo.

cmake --build . --config Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  ❌ Build failed!
    echo.
    echo  Check the output above for errors.
    echo.
    pause
    exit /b 1
)

echo.
echo  ✓ Build successful!
echo.

:: ============================================================================
:: Step 5: Verify Output
:: ============================================================================

echo  [5/5] Verifying output...
echo  ═══════════════════════════════════════════════════════════════
echo.

cd /d "%~dp0"

set PLUGIN_DLL=bin\Release\Plugin\Re_Kenshi_Plugin.dll

if exist "%PLUGIN_DLL%" (
    for %%F in ("%PLUGIN_DLL%") do set SIZE=%%~zF
    echo  ✓ Plugin DLL created: !SIZE! bytes
    echo.
    echo  Location: %PLUGIN_DLL%
) else (
    echo  ⚠  Plugin DLL not found at expected location
    echo.
    echo  Expected: %PLUGIN_DLL%
)

echo.

:: ============================================================================
:: Done
:: ============================================================================

echo  ╔═══════════════════════════════════════════════════════════════╗
echo  ║                  Build Complete!                              ║
echo  ╚═══════════════════════════════════════════════════════════════╝
echo.
echo  Plugin location:
echo    %PLUGIN_DLL%
echo.
echo  To use:
echo    1. Run PLAY.bat to start Kenshi Online
echo    2. Start Kenshi
echo    3. Inject %PLUGIN_DLL% into kenshi_x64.exe
echo.
pause
