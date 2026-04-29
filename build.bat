@echo off
setlocal enabledelayedexpansion

echo.
echo  ========================================
echo   KenshiMP / Kenshi-Online Build Script
echo  ========================================
echo.

:: ── Check prerequisites ──
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found in PATH.
    echo         Install CMake 3.20+ from https://cmake.org/download/
    echo         Or install via: winget install Kitware.CMake
    goto :fail
)

:: ── Detect Visual Studio ──
set "VS_GEN="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022" (
    set "VS_GEN=Visual Studio 17 2022"
    echo [OK] Found Visual Studio 2022
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019" (
    set "VS_GEN=Visual Studio 16 2019"
    echo [OK] Found Visual Studio 2019
) else (
    echo [ERROR] Visual Studio 2019 or 2022 not found.
    echo         Install Visual Studio with "Desktop development with C++" workload.
    goto :fail
)

:: ── Check submodules ──
if not exist "lib\enet\CMakeLists.txt" (
    echo [WARN] Submodules not initialized. Running git submodule update...
    git submodule update --init --recursive
    if errorlevel 1 (
        echo [ERROR] Failed to initialize submodules.
        echo         Make sure git is installed and you cloned with: git clone --recursive
        goto :fail
    )
)

:: ── Configure ──
echo.
echo [1/3] Configuring CMake (x64 Release)...
if not exist "build" mkdir build
cmake -G "%VS_GEN%" -A x64 -S . -B build
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    goto :fail
)

:: ── Build ──
echo.
echo [2/3] Building all targets (Release)...
cmake --build build --config Release
if errorlevel 1 (
    echo [ERROR] Build failed.
    goto :fail
)

:: ── Run tests ──
echo.
echo [3/3] Running unit tests...
build\bin\Release\KenshiMP.UnitTest.exe
if errorlevel 1 (
    echo [WARN] Some unit tests failed.
) else (
    echo [OK] All tests passed.
)

echo.
echo  ========================================
echo   BUILD SUCCESSFUL
echo  ========================================
echo.
echo  Output binaries in: build\bin\Release\
echo.
echo  Key files:
echo    KenshiMP.Core.dll      - Client plugin (auto-deployed to Kenshi dir)
echo    KenshiMP.Server.exe    - Dedicated server (auto-deployed to Kenshi dir)
echo    KenshiMP.Injector.exe  - Launcher / installer
echo.
echo  To open in Visual Studio:
echo    build\KenshiMP.sln
echo.
goto :end

:fail
echo.
echo  BUILD FAILED - see errors above.
echo.
exit /b 1

:end
endlocal
