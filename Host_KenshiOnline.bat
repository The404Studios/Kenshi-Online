@echo off
echo ╔════════════════════════════════════════════════════════╗
echo ║          Host Kenshi Online Server                     ║
echo ╚════════════════════════════════════════════════════════╝
echo.

set /p PORT="Enter port (default 7777): "
if "%PORT%"=="" set PORT=7777

echo.
echo Starting server on port %PORT%...
echo.
echo Instructions:
echo   1. Keep this window open
echo   2. Share your IP address with friends
echo   3. Make sure port %PORT% is forwarded in your router
echo   4. Use Join_KenshiOnline.bat on other machines to connect
echo.
echo Press Ctrl+C to stop the server
echo.

cd bin\Release\Server
start /B KenshiOnlineServer.exe %PORT%

echo Server is running!
echo.
pause
