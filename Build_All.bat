@echo off
:: Re_Kenshi - Build All Components Using Solution
:: This script builds the entire ReKenshi solution

color 0B
echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║          Re_Kenshi - Build All Components              ║
echo ╚════════════════════════════════════════════════════════╝
echo.

:: Check for MSBuild
where msbuild >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: MSBuild not found!
    echo.
    echo Please install Visual Studio 2022 with:
    echo   - Desktop development with C++
    echo   - .NET desktop development
    echo.
    pause
    exit /b 1
)

:: Check for CMake
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake not found!
    echo Download from: https://cmake.org/download/
    pause
    exit /b 1
)

echo ========================================
echo Step 1: Generate C++ Project
echo ========================================
echo.

cd /d "%~dp0Re_Kenshi_Plugin"

if not exist build mkdir build
cd build

echo Generating Visual Studio project...
cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake generation failed!
    cd /d "%~dp0"
    pause
    exit /b 1
)

echo ✓ C++ project generated
cd /d "%~dp0"

echo.
echo ========================================
echo Step 2: Building Entire Solution
echo ========================================
echo.

echo Building ReKenshi solution in Release mode...
echo This includes:
echo   - C++ Plugin (Re_Kenshi_Plugin.dll)
echo   - C# Server (ReKenshiServer.exe)
echo   - C# Client Service (ReKenshiClientService.exe)
echo.

:: Find MSBuild path
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set MSBUILD_PATH=%%i
)

if not defined MSBUILD_PATH (
    echo ERROR: Could not find MSBuild!
    echo Please install Visual Studio 2022
    pause
    exit /b 1
)

echo Using MSBuild: %MSBUILD_PATH%
echo.

"%MSBUILD_PATH%" ReKenshi.sln /p:Configuration=Release /p:Platform="Any CPU" /m /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Solution build failed!
    pause
    exit /b 1
)

echo.
echo Building C++ Plugin...
cd Re_Kenshi_Plugin\build
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: C++ build failed!
    cd /d "%~dp0"
    pause
    exit /b 1
)

cd /d "%~dp0"

echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║              BUILD SUCCESSFUL!                         ║
echo ╚════════════════════════════════════════════════════════╝
echo.
echo Output locations:
echo   Plugin:  Re_Kenshi_Plugin\build\bin\Release\Re_Kenshi_Plugin.dll
echo   Server:  ReKenshi.Server\bin\Release\net8.0\ReKenshiServer.exe
echo   Client:  ReKenshi.ClientService\bin\Release\net8.0\ReKenshiClientService.exe
echo.
echo All components built successfully!
echo.
pause
