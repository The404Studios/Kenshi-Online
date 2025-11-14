@echo off
:: Re_Kenshi Multiplayer - Network Test Tool
:: Tests if you can connect to a server

color 0E
echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║     Re_Kenshi - Network Connection Test Tool          ║
echo ╚════════════════════════════════════════════════════════╝
echo.
echo This tool helps diagnose connection problems.
echo.

set /p TEST_IP="Enter server IP to test (or press Enter for localhost): "

if "%TEST_IP%"=="" (
    set TEST_IP=localhost
    echo Testing localhost connection...
) else (
    echo Testing connection to %TEST_IP%...
)

echo.
echo ========================================
echo Test 1: Ping Test
echo ========================================
echo.

ping -n 4 %TEST_IP%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [FAIL] Cannot reach %TEST_IP%
    echo.
    echo Possible issues:
    echo   - Server is offline
    echo   - Wrong IP address
    echo   - Network connection problem
    echo   - Firewall blocking ping
    echo.
    pause
    exit /b 1
)

echo.
echo [PASS] Server is reachable!
echo.

echo ========================================
echo Test 2: Port 7777 Test
echo ========================================
echo.
echo Testing if port 7777 is open...
echo.

:: Try to connect using PowerShell
powershell -Command "& {$test = New-Object System.Net.Sockets.TcpClient; try { $test.Connect('%TEST_IP%', 7777); $test.Close(); Write-Host '[PASS] Port 7777 is open and accepting connections!' -ForegroundColor Green; exit 0 } catch { Write-Host '[FAIL] Port 7777 is closed or unreachable' -ForegroundColor Red; Write-Host ''; Write-Host 'Possible issues:' -ForegroundColor Yellow; Write-Host '  - Server not running (run Host_Server.bat)' -ForegroundColor Yellow; Write-Host '  - Port not forwarded in router' -ForegroundColor Yellow; Write-Host '  - Firewall blocking port 7777' -ForegroundColor Yellow; exit 1 }}"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo Troubleshooting Steps:
    echo ========================================
    echo.
    echo 1. Make sure server is running:
    echo    - Run Host_Server.bat on server machine
    echo.
    echo 2. Check firewall:
    echo    - Windows Firewall might be blocking port 7777
    echo    - Add exception for ReKenshiServer.exe
    echo.
    echo 3. Port forwarding:
    echo    - Login to router (usually 192.168.1.1)
    echo    - Forward port 7777 TCP to server PC
    echo.
    echo 4. Try local network first:
    echo    - Use server's local IP (192.168.x.x)
    echo    - If that works, issue is with port forwarding
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo Connection Test Results
echo ========================================
echo.
echo ✓ Server is reachable
echo ✓ Port 7777 is open
echo ✓ Connection successful!
echo.
echo You should be able to connect to this server.
echo.
echo Next steps:
echo   1. Run Join_Friend.bat
echo   2. Enter %TEST_IP% when prompted
echo   3. Start Kenshi and inject the plugin
echo.
pause
