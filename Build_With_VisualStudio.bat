@echo off
:: Re_Kenshi - Open in Visual Studio
:: Opens the ReKenshi solution in Visual Studio for development

color 0E
echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║     Re_Kenshi - Open in Visual Studio                 ║
echo ╚════════════════════════════════════════════════════════╝
echo.

:: Check if solution exists
if not exist "ReKenshi.sln" (
    echo ERROR: ReKenshi.sln not found!
    echo Please run this from the Kenshi-Online directory.
    pause
    exit /b 1
)

:: Generate C++ project first if needed
if not exist "Re_Kenshi_Plugin\build\Re_Kenshi_Plugin.sln" (
    echo Generating C++ Visual Studio project...
    cd Re_Kenshi_Plugin
    if not exist build mkdir build
    cd build
    cmake .. -G "Visual Studio 17 2022" -A x64
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: CMake generation failed!
        echo Make sure CMake is installed.
        cd /d "%~dp0"
        pause
        exit /b 1
    )
    cd /d "%~dp0"
    echo ✓ C++ project generated
    echo.
)

echo Opening ReKenshi.sln in Visual Studio...
echo.
echo The solution includes:
echo   1. ReKenshi.Server (C# Game Server)
echo   2. ReKenshi.ClientService (C# Client Service)
echo   3. Re_Kenshi_Plugin (C++ Plugin - via CMakeLists.txt)
echo.
echo You can build all projects from Visual Studio:
echo   - Build ^> Build Solution (Ctrl+Shift+B)
echo   - Or right-click solution ^> Build Solution
echo.

:: Try to open with Visual Studio
start ReKenshi.sln

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Could not auto-open Visual Studio.
    echo Please open ReKenshi.sln manually.
    echo.
)

echo Visual Studio should open now...
echo.
pause
