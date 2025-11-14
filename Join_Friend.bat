@echo off
:: Re_Kenshi Multiplayer - Join Friend
:: This script connects to a friend's server

color 0A
echo.
echo ========================================
echo   Re_Kenshi Multiplayer - Join Friend
echo ========================================
echo.

set /p SERVER_IP="Enter your friend's IP address: "

if "%SERVER_IP%"=="" (
    echo ERROR: No IP address provided!
    pause
    exit /b 1
)

echo.
echo Connecting to %SERVER_IP%:7777...
echo.
echo ========================================
echo.

cd /d "%~dp0ReKenshi.ClientService\bin\Release\net8.0"

if not exist ReKenshiClientService.exe (
    echo ERROR: Client service not built!
    echo Please run: cd ReKenshi.ClientService ^&^& dotnet build -c Release
    pause
    exit /b 1
)

ReKenshiClientService.exe %SERVER_IP% 7777
pause
