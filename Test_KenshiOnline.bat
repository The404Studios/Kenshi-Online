@echo off
echo ╔════════════════════════════════════════════════════════╗
echo ║          Kenshi Online Integration Test                ║
echo ╚════════════════════════════════════════════════════════╝
echo.

echo This script will test the complete Kenshi Online system.
echo.
echo Test steps:
echo   1. Start server
echo   2. Start client service
echo   3. Verify connections
echo   4. Clean up
echo.
pause

echo [1/4] Starting server...
start "Kenshi Online Server" cmd /k "cd bin\Release\Server && KenshiOnlineServer.exe 7777"
timeout /t 3 /nobreak > nul

echo [2/4] Starting client service...
start "Kenshi Online Client" cmd /k "cd bin\Release\ClientService && KenshiOnlineClientService.exe 127.0.0.1 7777"
timeout /t 3 /nobreak > nul

echo [3/4] Services started!
echo.
echo Check the windows that opened:
echo   - Server should show "Waiting for players..."
echo   - Client should show "Connecting to server..."
echo.
echo If both are running correctly, the test is successful!
echo.
echo Press any key to stop services...
pause > nul

echo [4/4] Stopping services...
taskkill /FI "WINDOWTITLE eq Kenshi Online Server*" /F
taskkill /FI "WINDOWTITLE eq Kenshi Online Client*" /F

echo.
echo Test complete!
pause
