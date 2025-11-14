@echo off
:: Re_Kenshi Multiplayer - Play on Localhost
:: For testing or LAN play

color 0B
echo.
echo ========================================
echo   Re_Kenshi - Play on Localhost/LAN
echo ========================================
echo.
echo Connecting to localhost (127.0.0.1)...
echo This is for:
echo   - Testing
echo   - LAN play (server on same network)
echo.
echo ========================================
echo.

cd /d "%~dp0ReKenshi.ClientService\bin\Release\net8.0"

if not exist ReKenshiClientService.exe (
    echo ERROR: Client service not built!
    echo Please run Setup_First_Time.bat first
    pause
    exit /b 1
)

ReKenshiClientService.exe localhost 7777
pause
