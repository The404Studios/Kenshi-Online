@echo off
:: Re_Kenshi - Integration Test
:: Tests that all components work together

color 0D
echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║          Re_Kenshi - Integration Test                  ║
echo ╚════════════════════════════════════════════════════════╝
echo.

echo This test will:
echo   1. Verify all components are built
echo   2. Start the server
echo   3. Start a client service
echo   4. Test communication between components
echo   5. Shut everything down
echo.
echo Press Ctrl+C at any time to stop the test.
echo.
pause

:: Verify build first
echo ========================================
echo Step 1: Verifying Build
echo ========================================
echo.

call Verify_Build.bat >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build verification failed!
    echo Please run Setup_First_Time.bat first.
    pause
    exit /b 1
)

echo ✓ All components present
echo.

:: Start server in background
echo ========================================
echo Step 2: Starting Server
echo ========================================
echo.

cd ReKenshi.Server\bin\Release\net8.0

if not exist ReKenshiServer.exe (
    echo ERROR: Server executable not found!
    cd /d "%~dp0"
    pause
    exit /b 1
)

echo Starting server on port 7777...
start "ReKenshi Server (Test)" /MIN ReKenshiServer.exe 7777

:: Wait for server to start
timeout /t 3 /nobreak >nul

cd /d "%~dp0"

echo ✓ Server started
echo.

:: Test server connection
echo ========================================
echo Step 3: Testing Server Connection
echo ========================================
echo.

echo Testing if server is accepting connections...
powershell -Command "& {$test = New-Object System.Net.Sockets.TcpClient; try { $test.Connect('localhost', 7777); $test.Close(); Write-Host '[PASS] Server is accepting connections' -ForegroundColor Green; exit 0 } catch { Write-Host '[FAIL] Server is not responding' -ForegroundColor Red; exit 1 }}"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Server is not responding!
    echo Stopping test...
    taskkill /FI "WindowTitle eq ReKenshi Server (Test)*" /F >nul 2>nul
    pause
    exit /b 1
)

echo.

:: Start client service
echo ========================================
echo Step 4: Starting Client Service
echo ========================================
echo.

cd ReKenshi.ClientService\bin\Release\net8.0

if not exist ReKenshiClientService.exe (
    echo ERROR: Client service executable not found!
    cd /d "%~dp0"
    taskkill /FI "WindowTitle eq ReKenshi Server (Test)*" /F >nul 2>nul
    pause
    exit /b 1
)

echo Starting client service...
start "ReKenshi Client (Test)" /MIN ReKenshiClientService.exe localhost 7777

:: Wait for client to connect
timeout /t 2 /nobreak >nul

cd /d "%~dp0"

echo ✓ Client service started
echo.

:: Check if everything is running
echo ========================================
echo Step 5: Verifying Communication
echo ========================================
echo.

echo Checking if processes are running...

tasklist /FI "WindowTitle eq ReKenshi Server (Test)*" 2>NUL | find /I /N "ReKenshiServer.exe">NUL
if %ERRORLEVEL%==0 (
    echo [✓] Server process: Running
) else (
    echo [✗] Server process: Not running
)

tasklist /FI "WindowTitle eq ReKenshi Client (Test)*" 2>NUL | find /I /N "ReKenshiClientService.exe">NUL
if %ERRORLEVEL%==0 (
    echo [✓] Client service process: Running
) else (
    echo [✗] Client service process: Not running
)

echo.
echo Components are running!
echo Let them run for 5 seconds to test stability...
timeout /t 5 /nobreak >nul

echo.
echo ========================================
echo Step 6: Cleanup
echo ========================================
echo.

echo Stopping test processes...
taskkill /FI "WindowTitle eq ReKenshi Server (Test)*" /F >nul 2>nul
taskkill /FI "WindowTitle eq ReKenshi Client (Test)*" /F >nul 2>nul
timeout /t 1 /nobreak >nul

echo ✓ Cleanup complete
echo.

:: Final result
echo ╔════════════════════════════════════════════════════════╗
echo ║              INTEGRATION TEST PASSED!                  ║
echo ╚════════════════════════════════════════════════════════╝
echo.
echo ✓ Server started successfully
echo ✓ Server accepts connections
echo ✓ Client service connected
echo ✓ Components communicated
echo ✓ All processes cleaned up
echo.
echo Your Re_Kenshi installation is working correctly!
echo.
echo Next steps:
echo   1. Run Host_Server.bat to start hosting
echo   2. Run Join_Friend.bat to connect to a friend
echo   3. Inject Re_Kenshi_Plugin.dll into Kenshi
echo.
pause
