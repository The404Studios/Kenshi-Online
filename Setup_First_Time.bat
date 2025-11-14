@echo off
:: Re_Kenshi Multiplayer - First Time Setup
:: This script builds all components

color 0E
echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║     Re_Kenshi Multiplayer - First Time Setup          ║
echo ╚════════════════════════════════════════════════════════╝
echo.
echo This will build:
echo   1. C++ Plugin (Re_Kenshi_Plugin.dll)
echo   2. C# Server (ReKenshiServer.exe)
echo   3. C# Client Service (ReKenshiClientService.exe)
echo.
echo Requirements:
echo   - CMake
echo   - Visual Studio (C++)
echo   - .NET 8.0 SDK
echo.
pause

:: Check for CMake
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake not found!
    echo Download from: https://cmake.org/download/
    pause
    exit /b 1
)

:: Check for .NET
where dotnet >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: .NET SDK not found!
    echo Download from: https://dotnet.microsoft.com/download
    pause
    exit /b 1
)

echo.
echo ========================================
echo Building C++ Plugin...
echo ========================================
echo.

cd /d "%~dp0Re_Kenshi_Plugin"

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: CMake configuration failed!
    echo Make sure Visual Studio is installed.
    pause
    exit /b 1
)

cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: C++ build failed!
    pause
    exit /b 1
)

echo.
echo ✓ C++ Plugin built successfully!
echo   Location: Re_Kenshi_Plugin\build\bin\Release\Re_Kenshi_Plugin.dll
echo.

cd /d "%~dp0"

echo.
echo ========================================
echo Building C# Server...
echo ========================================
echo.

cd ReKenshi.Server
dotnet build -c Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Server build failed!
    pause
    exit /b 1
)

echo.
echo ✓ Server built successfully!
echo   Location: ReKenshi.Server\bin\Release\net8.0\ReKenshiServer.exe
echo.

cd ..

echo.
echo ========================================
echo Building C# Client Service...
echo ========================================
echo.

cd ReKenshi.ClientService
dotnet build -c Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Client service build failed!
    pause
    exit /b 1
)

echo.
echo ✓ Client service built successfully!
echo   Location: ReKenshi.ClientService\bin\Release\net8.0\ReKenshiClientService.exe
echo.

cd ..

echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║              BUILD COMPLETE!                           ║
echo ╚════════════════════════════════════════════════════════╝
echo.
echo Next steps:
echo.
echo TO HOST A SERVER:
echo   1. Run: Host_Server.bat
echo   2. Forward port 7777 in your router
echo   3. Give friends your IP from https://whatismyipaddress.com
echo.
echo TO JOIN A FRIEND:
echo   1. Run: Join_Friend.bat
echo   2. Enter your friend's IP address
echo.
echo TO PLAY:
echo   1. Start Kenshi
echo   2. Use a DLL injector to inject Re_Kenshi_Plugin.dll
echo   3. You'll see a confirmation message when it loads
echo.
echo RECOMMENDED DLL INJECTOR:
echo   - Extreme Injector: https://github.com/master131/ExtremeInjector
echo.
pause
