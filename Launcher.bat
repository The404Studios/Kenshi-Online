@echo off
setlocal enabledelayedexpansion

:MENU
cls
echo ╔════════════════════════════════════════════════════════╗
echo ║          Kenshi Online Launcher v2.0                   ║
echo ╚════════════════════════════════════════════════════════╝
echo.
echo   [1] Host Server
echo   [2] Join Server
echo   [3] Build All
echo   [4] Test System
echo   [5] View Quick Start Guide
echo   [6] View Full Documentation
echo   [7] Exit
echo.
set /p choice="Select an option (1-7): "

if "%choice%"=="1" goto HOST
if "%choice%"=="2" goto JOIN
if "%choice%"=="3" goto BUILD
if "%choice%"=="4" goto TEST
if "%choice%"=="5" goto QUICKSTART
if "%choice%"=="6" goto DOCS
if "%choice%"=="7" goto EXIT
echo Invalid choice! Press any key to try again...
pause > nul
goto MENU

:HOST
cls
echo ╔════════════════════════════════════════════════════════╗
echo ║          Host Kenshi Online Server                     ║
echo ╚════════════════════════════════════════════════════════╝
echo.
call Host_KenshiOnline.bat
goto MENU

:JOIN
cls
echo ╔════════════════════════════════════════════════════════╗
echo ║          Join Kenshi Online Server                     ║
echo ╚════════════════════════════════════════════════════════╝
echo.
call Join_KenshiOnline.bat
goto MENU

:BUILD
cls
echo ╔════════════════════════════════════════════════════════╗
echo ║          Build Kenshi Online                           ║
echo ╚════════════════════════════════════════════════════════╝
echo.
call Build_KenshiOnline.bat
goto MENU

:TEST
cls
echo ╔════════════════════════════════════════════════════════╗
echo ║          Test Kenshi Online System                     ║
echo ╚════════════════════════════════════════════════════════╝
echo.
call Test_KenshiOnline.bat
goto MENU

:QUICKSTART
cls
if exist QUICK_START_V2.md (
    type QUICK_START_V2.md | more
) else (
    echo Quick Start Guide not found!
)
echo.
pause
goto MENU

:DOCS
cls
if exist KENSHI_ONLINE_V2.md (
    type KENSHI_ONLINE_V2.md | more
) else (
    echo Documentation not found!
)
echo.
pause
goto MENU

:EXIT
cls
echo Thank you for using Kenshi Online!
echo.
timeout /t 2 /nobreak > nul
exit /b 0
