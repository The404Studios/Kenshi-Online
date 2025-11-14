@echo off
:: Re_Kenshi Multiplayer - Host Server
:: This script starts a server on your PC for friends to join
:: Make sure to forward port 7777 in your router!

color 0A
echo.
echo ========================================
echo   Re_Kenshi Multiplayer - Host Server
echo ========================================
echo.
echo Starting server on port 7777...
echo Friends can join using your public IP
echo.
echo To find your public IP, visit: https://whatismyipaddress.com
echo.
echo Press Ctrl+C to stop the server
echo ========================================
echo.

cd /d "%~dp0ReKenshi.Server\bin\Release\net8.0"

if not exist ReKenshiServer.exe (
    echo ERROR: Server not built!
    echo Please run: cd ReKenshi.Server ^&^& dotnet build -c Release
    pause
    exit /b 1
)

ReKenshiServer.exe 7777
pause
