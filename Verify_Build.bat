@echo off
:: Re_Kenshi - Build Verification
:: Checks if all components were built successfully

color 0A
echo.
echo ╔════════════════════════════════════════════════════════╗
echo ║          Re_Kenshi - Build Verification                ║
echo ╚════════════════════════════════════════════════════════╝
echo.

set SUCCESS=1

echo Checking build outputs...
echo.

:: Check C++ Plugin
set PLUGIN_PATH=Re_Kenshi_Plugin\build\bin\Release\Re_Kenshi_Plugin.dll
if exist "%PLUGIN_PATH%" (
    echo [✓] C++ Plugin: Found
    for %%A in ("%PLUGIN_PATH%") do echo     Size: %%~zA bytes
    echo     Path: %PLUGIN_PATH%
) else (
    echo [✗] C++ Plugin: NOT FOUND
    echo     Expected: %PLUGIN_PATH%
    set SUCCESS=0
)
echo.

:: Check C# Server
set SERVER_PATH=ReKenshi.Server\bin\Release\net8.0\ReKenshiServer.exe
if exist "%SERVER_PATH%" (
    echo [✓] C# Server: Found
    for %%A in ("%SERVER_PATH%") do echo     Size: %%~zA bytes
    echo     Path: %SERVER_PATH%
) else (
    echo [✗] C# Server: NOT FOUND
    echo     Expected: %SERVER_PATH%
    set SUCCESS=0
)
echo.

:: Check C# Client Service
set CLIENT_PATH=ReKenshi.ClientService\bin\Release\net8.0\ReKenshiClientService.exe
if exist "%CLIENT_PATH%" (
    echo [✓] C# Client Service: Found
    for %%A in ("%CLIENT_PATH%") do echo     Size: %%~zA bytes
    echo     Path: %CLIENT_PATH%
) else (
    echo [✗] C# Client Service: NOT FOUND
    echo     Expected: %CLIENT_PATH%
    set SUCCESS=0
)
echo.

:: Check launcher scripts
echo Checking launcher scripts...
echo.

if exist "Host_Server.bat" (
    echo [✓] Host_Server.bat
) else (
    echo [✗] Host_Server.bat
    set SUCCESS=0
)

if exist "Join_Friend.bat" (
    echo [✓] Join_Friend.bat
) else (
    echo [✗] Join_Friend.bat
    set SUCCESS=0
)

if exist "Play_Localhost.bat" (
    echo [✓] Play_Localhost.bat
) else (
    echo [✗] Play_Localhost.bat
    set SUCCESS=0
)

if exist "Test_Connection.bat" (
    echo [✓] Test_Connection.bat
) else (
    echo [✗] Test_Connection.bat
    set SUCCESS=0
)
echo.

:: Check documentation
echo Checking documentation...
echo.

if exist "QUICK_START.md" (
    echo [✓] QUICK_START.md
) else (
    echo [✗] QUICK_START.md
    set SUCCESS=0
)

if exist "MULTIPLAYER_SETUP.md" (
    echo [✓] MULTIPLAYER_SETUP.md
) else (
    echo [✗] MULTIPLAYER_SETUP.md
    set SUCCESS=0
)

if exist "KSERVERMOD_INTEGRATION.md" (
    echo [✓] KSERVERMOD_INTEGRATION.md
) else (
    echo [✗] KSERVERMOD_INTEGRATION.md
    set SUCCESS=0
)
echo.

:: Check solution file
echo Checking solution...
echo.

if exist "ReKenshi.sln" (
    echo [✓] ReKenshi.sln
) else (
    echo [✗] ReKenshi.sln
    set SUCCESS=0
)
echo.

:: Final result
echo ========================================
if %SUCCESS%==1 (
    echo.
    echo ╔════════════════════════════════════════════════════════╗
    echo ║              ALL CHECKS PASSED!                        ║
    echo ╚════════════════════════════════════════════════════════╝
    echo.
    echo ✓ All components built successfully
    echo ✓ All launcher scripts present
    echo ✓ All documentation present
    echo ✓ Solution file present
    echo.
    echo You are ready to use Re_Kenshi!
    echo.
    echo Next steps:
    echo   1. Run Host_Server.bat to host a game
    echo   2. Run Join_Friend.bat to join a friend
    echo   3. See QUICK_START.md for instructions
    echo.
) else (
    echo.
    echo ╔════════════════════════════════════════════════════════╗
    echo ║              VERIFICATION FAILED!                      ║
    echo ╚════════════════════════════════════════════════════════╝
    echo.
    echo Some components are missing.
    echo.
    echo Please run Setup_First_Time.bat to build everything.
    echo.
)

pause
exit /b %SUCCESS%
