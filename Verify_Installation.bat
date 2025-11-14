@echo off
REM Kenshi Online - Installation Verification Script
REM Checks all dependencies and build status
echo.
echo ========================================
echo   KENSHI ONLINE - INSTALLATION CHECK
echo ========================================
echo.

set ERRORS=0
set WARNINGS=0

REM Color codes for output
REM [OK] = Green check
REM [WARN] = Yellow warning
REM [FAIL] = Red X

echo Checking installation requirements...
echo.

REM ========================================
REM CHECK 1: .NET 8.0 SDK
REM ========================================
echo [1/8] Checking .NET 8.0 SDK...
dotnet --version >nul 2>&1
if %errorlevel% equ 0 (
    for /f "tokens=*" %%i in ('dotnet --version') do set DOTNET_VERSION=%%i
    echo    [OK] .NET SDK found: v!DOTNET_VERSION!
) else (
    echo    [FAIL] .NET 8.0 SDK not found
    echo           Download: https://dotnet.microsoft.com/download
    set /a ERRORS+=1
)
echo.

REM ========================================
REM CHECK 2: Visual Studio 2022
REM ========================================
echo [2/8] Checking Visual Studio 2022...
set VS_PATH=
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe" (
    set VS_PATH=Community
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.exe" (
    set VS_PATH=Professional
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\devenv.exe" (
    set VS_PATH=Enterprise
)

if defined VS_PATH (
    echo    [OK] Visual Studio 2022 found: !VS_PATH!
) else (
    echo    [WARN] Visual Studio 2022 not found
    echo           Required for C++ plugin build
    echo           Download: https://visualstudio.microsoft.com/downloads/
    set /a WARNINGS+=1
)
echo.

REM ========================================
REM CHECK 3: CMake
REM ========================================
echo [3/8] Checking CMake...
cmake --version >nul 2>&1
if %errorlevel% equ 0 (
    for /f "tokens=3" %%i in ('cmake --version ^| findstr /C:"cmake version"') do set CMAKE_VERSION=%%i
    echo    [OK] CMake found: v!CMAKE_VERSION!
) else (
    echo    [WARN] CMake not found
    echo           Required for C++ plugin build
    echo           Download: https://cmake.org/download/
    set /a WARNINGS+=1
)
echo.

REM ========================================
REM CHECK 4: Directory Structure
REM ========================================
echo [4/8] Checking directory structure...
set DIR_OK=1

if not exist "KenshiOnline.Launcher" (
    echo    [FAIL] KenshiOnline.Launcher directory missing
    set DIR_OK=0
    set /a ERRORS+=1
)

if not exist "KenshiOnline.Core" (
    echo    [FAIL] KenshiOnline.Core directory missing
    set DIR_OK=0
    set /a ERRORS+=1
)

if not exist "Re_Kenshi_Plugin" (
    echo    [FAIL] Re_Kenshi_Plugin directory missing
    set DIR_OK=0
    set /a ERRORS+=1
)

if %DIR_OK% equ 1 (
    echo    [OK] All project directories present
)
echo.

REM ========================================
REM CHECK 5: C++ Dependencies
REM ========================================
echo [5/8] Checking C++ dependencies...
if exist "Re_Kenshi_Plugin\vendor\nlohmann\json.hpp" (
    for %%A in ("Re_Kenshi_Plugin\vendor\nlohmann\json.hpp") do set SIZE=%%~zA
    if !SIZE! GTR 100000 (
        echo    [OK] nlohmann/json library found ^(!SIZE! bytes^)
    ) else (
        echo    [WARN] nlohmann/json is stub file ^(!SIZE! bytes^)
        echo           Run: Re_Kenshi_Plugin\Download_Dependencies.bat
        set /a WARNINGS+=1
    )
) else (
    echo    [FAIL] nlohmann/json library missing
    echo           Run: Re_Kenshi_Plugin\Download_Dependencies.bat
    set /a ERRORS+=1
)
echo.

REM ========================================
REM CHECK 6: C++ Plugin Build
REM ========================================
echo [6/8] Checking C++ plugin build...
if exist "bin\Release\Plugin\Re_Kenshi_Plugin.dll" (
    for %%A in ("bin\Release\Plugin\Re_Kenshi_Plugin.dll") do set DLL_SIZE=%%~zA
    echo    [OK] C++ plugin built ^(!DLL_SIZE! bytes^)
    echo           Location: bin\Release\Plugin\Re_Kenshi_Plugin.dll
) else (
    echo    [WARN] C++ plugin not built yet
    echo           Run: Build_Plugin.bat
    set /a WARNINGS+=1
)
echo.

REM ========================================
REM CHECK 7: Launcher Build
REM ========================================
echo [7/8] Checking launcher build...
if exist "bin\Release\KenshiOnline.exe" (
    for %%A in ("bin\Release\KenshiOnline.exe") do set EXE_SIZE=%%~zA
    echo    [OK] Launcher built ^(!EXE_SIZE! bytes^)
    echo           Location: bin\Release\KenshiOnline.exe
) else (
    echo    [WARN] Launcher not built yet
    echo           Will build automatically when you run PLAY.bat
    set /a WARNINGS+=1
)
echo.

REM ========================================
REM CHECK 8: Configuration Files
REM ========================================
echo [8/8] Checking configuration files...
if exist "bin\Release\kenshi_online.json" (
    echo    [OK] Configuration file exists
) else (
    echo    [WARN] Configuration file not created yet
    echo           Will be created automatically on first run
    set /a WARNINGS+=1
)

if exist "config-examples\server-config-example.json" (
    echo    [OK] Example configurations available
) else (
    echo    [WARN] Example configurations missing
    set /a WARNINGS+=1
)
echo.

REM ========================================
REM SUMMARY
REM ========================================
echo ========================================
echo   VERIFICATION SUMMARY
echo ========================================
echo.

if %ERRORS% equ 0 (
    if %WARNINGS% equ 0 (
        echo    STATUS: [OK] Installation ready!
        echo.
        echo    Next steps:
        echo    1. Run Build_Plugin.bat (first time only)
        echo    2. Run PLAY.bat to launch
        echo    3. Start Kenshi and inject the plugin
        echo.
    ) else (
        echo    STATUS: [WARN] Installation mostly ready
        echo    Warnings: !WARNINGS!
        echo.
        echo    You can proceed, but some features may not work.
        echo    Check warnings above for details.
        echo.
    )
) else (
    echo    STATUS: [FAIL] Installation incomplete
    echo    Errors: !ERRORS!
    echo    Warnings: !WARNINGS!
    echo.
    echo    Please fix the errors above before proceeding.
    echo.
)

echo ========================================
echo.
pause
