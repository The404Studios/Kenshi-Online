@echo off
echo ========================================
echo  Kenshi-Online Build Setup
echo ========================================
echo.

:: Check for vcpkg
where vcpkg >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: vcpkg not found in PATH!
    echo Please install vcpkg: https://vcpkg.io/en/getting-started.html
    echo Then add it to your PATH.
    pause
    exit /b 1
)

echo Installing dependencies via vcpkg...
echo.

vcpkg install enet:x64-windows
vcpkg install minhook:x64-windows
vcpkg install imgui[dx11-binding,win32-binding]:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install spdlog:x64-windows

echo.
echo Dependencies installed!
echo.

:: Find vcpkg toolchain
for /f "tokens=*" %%i in ('vcpkg integrate install 2^>^&1') do set VCPKG_OUTPUT=%%i

:: Configure CMake
echo Configuring CMake...
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake

if %errorlevel% neq 0 (
    echo.
    echo CMake configure failed! Make sure VCPKG_ROOT is set.
    echo Try: set VCPKG_ROOT=C:\path\to\vcpkg
    pause
    exit /b 1
)

echo.
echo ========================================
echo  Setup complete!
echo  Open build\KenshiMP.sln in Visual Studio 2022
echo  Or build from command line:
echo    cmake --build build --config Release
echo ========================================
pause
