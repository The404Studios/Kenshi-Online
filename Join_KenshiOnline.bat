@echo off
echo ╔════════════════════════════════════════════════════════╗
echo ║          Join Kenshi Online Server                     ║
echo ╚════════════════════════════════════════════════════════╝
echo.

set /p SERVER_IP="Enter server IP address: "
set /p PORT="Enter server port (default 7777): "
if "%PORT%"=="" set PORT=7777

echo.
echo Connecting to %SERVER_IP%:%PORT%...
echo.
echo Instructions:
echo   1. Keep this window open while playing
echo   2. Make sure Kenshi is running with the plugin injected
echo   3. This will bridge between the game and the server
echo.
echo Press Ctrl+C to disconnect
echo.

cd bin\Release\ClientService
start /B KenshiOnlineClientService.exe %SERVER_IP% %PORT%

echo Client service is running!
echo.
pause
