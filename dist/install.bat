@echo off
setlocal enabledelayedexpansion
title KenshiMP Installer
color 0A

echo.
echo  ============================================
echo   KenshiMP - Kenshi Multiplayer Mod
echo   One-Click Installer
echo   made with love by fourzerofour
echo  ============================================
echo.

:: ── Auto-detect Kenshi directory ──
:: Try common locations, then fall back to asking

set "KENSHI_DIR="

:: Check if we're already in the Kenshi folder
if exist "%~dp0kenshi_x64.exe" (
    set "KENSHI_DIR=%~dp0"
    goto :found_kenshi
)

:: Check if we're in a subfolder of Kenshi
if exist "%~dp0..\kenshi_x64.exe" (
    set "KENSHI_DIR=%~dp0..\"
    goto :found_kenshi
)

:: Try Steam default location
set "STEAM_KENSHI=C:\Program Files (x86)\Steam\steamapps\common\Kenshi"
if exist "%STEAM_KENSHI%\kenshi_x64.exe" (
    set "KENSHI_DIR=%STEAM_KENSHI%"
    goto :found_kenshi
)

:: Try GOG default
set "GOG_KENSHI=C:\GOG Games\Kenshi"
if exist "%GOG_KENSHI%\kenshi_x64.exe" (
    set "KENSHI_DIR=%GOG_KENSHI%"
    goto :found_kenshi
)

:: Ask user
echo  Could not auto-detect Kenshi installation.
echo  Please enter the path to your Kenshi folder:
echo  (The folder containing kenshi_x64.exe)
echo.
set /p "KENSHI_DIR=Path: "

if not exist "%KENSHI_DIR%\kenshi_x64.exe" (
    echo.
    echo  [ERROR] kenshi_x64.exe not found at: %KENSHI_DIR%
    echo  Make sure you entered the correct Kenshi folder.
    echo.
    pause
    exit /b 1
)

:found_kenshi
:: Remove trailing backslash if present
if "%KENSHI_DIR:~-1%"=="\" set "KENSHI_DIR=%KENSHI_DIR:~0,-1%"

echo  Found Kenshi at: %KENSHI_DIR%
echo.

:: ── Check if Kenshi is running ──
tasklist /FI "IMAGENAME eq kenshi_x64.exe" 2>NUL | find /I "kenshi_x64.exe" >NUL
if %errorlevel% equ 0 (
    echo  [WARNING] Kenshi is currently running!
    echo  Please close Kenshi before installing.
    echo.
    pause
    exit /b 1
)

:: ── Create backups ──
echo  [1/5] Creating backups...

set "BACKUP_DIR=%KENSHI_DIR%\KenshiMP_backup"
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"

if exist "%KENSHI_DIR%\Plugins_x64.cfg" (
    if not exist "%BACKUP_DIR%\Plugins_x64.cfg.bak" (
        copy /Y "%KENSHI_DIR%\Plugins_x64.cfg" "%BACKUP_DIR%\Plugins_x64.cfg.bak" >nul
        echo         Backed up Plugins_x64.cfg
    )
)

if exist "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" (
    if not exist "%BACKUP_DIR%\Kenshi_MainMenu.layout.bak" (
        copy /Y "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" "%BACKUP_DIR%\Kenshi_MainMenu.layout.bak" >nul
        echo         Backed up Kenshi_MainMenu.layout
    )
)

:: ── Copy DLL ──
echo  [2/5] Installing KenshiMP.Core.dll...

if exist "%~dp0KenshiMP.Core.dll" (
    copy /Y "%~dp0KenshiMP.Core.dll" "%KENSHI_DIR%\KenshiMP.Core.dll" >nul
    if errorlevel 1 (
        echo  [ERROR] Failed to copy DLL. Is Kenshi running?
        pause
        exit /b 1
    )
    echo         Copied KenshiMP.Core.dll
) else (
    echo  [ERROR] KenshiMP.Core.dll not found in installer folder!
    pause
    exit /b 1
)

:: ── Patch Plugins_x64.cfg ──
echo  [3/5] Patching Plugins_x64.cfg...

findstr /C:"Plugin=KenshiMP.Core" "%KENSHI_DIR%\Plugins_x64.cfg" >nul 2>&1
if errorlevel 1 (
    echo Plugin=KenshiMP.Core>> "%KENSHI_DIR%\Plugins_x64.cfg"
    echo         Added KenshiMP.Core plugin entry
) else (
    echo         Plugin entry already exists
)

:: ── Patch Main Menu Layout ──
echo  [4/5] Patching main menu layout...

findstr /C:"MultiplayerButton" "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" >nul 2>&1
if errorlevel 1 (
    echo         Adding MULTIPLAYER button to main menu...

    :: We need to insert the button before the OPTIONS button.
    :: The OPTIONS button has name="OptionsButton".
    :: We'll use PowerShell for reliable XML insertion.

    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$file = '%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout';" ^
        "$content = Get-Content $file -Raw;" ^
        "$mpButton = @'" ^
"            <Widget type=\""Button\"" skin=\""Kenshi_Button1\"" position_real=\""0.260417 0.582407 0.15625 0.0638889\"" name=\""MultiplayerButton\"">`n" ^
"                <Property key=\""Caption\"" value=\""MULTIPLAYER\""/>`n" ^
"                <Property key=\""FontName\"" value=\""Kenshi_PaintedTextFont_Large\""/>`n" ^
"            </Widget>`n" ^
"'@;" ^
        "if ($content -match 'OptionsButton') {" ^
        "  $optLine = '            <Widget type=\"\"Button\"\" skin=\"\"Kenshi_Button1\"\".*name=\"\"OptionsButton\"\"';" ^
        "  $content = $content -replace '(?m)(^\s*<Widget[^>]+name=\"\"OptionsButton\"\")', ($mpButton + \"`n`$1\");" ^
        "  Set-Content $file $content -NoNewline;" ^
        "  Write-Host '        MULTIPLAYER button added';" ^
        "} else {" ^
        "  Write-Host '        [WARNING] Could not find OptionsButton anchor';" ^
        "}"

    if errorlevel 1 (
        echo         [WARNING] Auto-patch failed. Copying pre-patched layout...
        if exist "%~dp0Kenshi_MainMenu.layout" (
            copy /Y "%~dp0Kenshi_MainMenu.layout" "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" >nul
            echo         Copied pre-patched Kenshi_MainMenu.layout
        )
    )
) else (
    echo         MULTIPLAYER button already present
)

:: ── Copy Multiplayer Layouts ──
echo  [5/6] Installing multiplayer layouts...

if exist "%~dp0Kenshi_MultiplayerPanel.layout" (
    copy /Y "%~dp0Kenshi_MultiplayerPanel.layout" "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout" >nul
    echo         Copied Kenshi_MultiplayerPanel.layout
) else (
    echo  [ERROR] Kenshi_MultiplayerPanel.layout not found in installer folder!
    pause
    exit /b 1
)

if exist "%~dp0Kenshi_MultiplayerHUD.layout" (
    copy /Y "%~dp0Kenshi_MultiplayerHUD.layout" "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout" >nul
    echo         Copied Kenshi_MultiplayerHUD.layout
) else (
    echo  [WARNING] Kenshi_MultiplayerHUD.layout not found (in-game HUD may not work)
)

:: ── Install Multiplayer Mod ──
echo  [6/7] Installing kenshi-online.mod...

if exist "%~dp0kenshi-online.mod" (
    :: Copy to data/ (always loaded by the game engine)
    copy /Y "%~dp0kenshi-online.mod" "%KENSHI_DIR%\data\kenshi-online.mod" >nul
    echo         Copied kenshi-online.mod to data/

    :: Also copy to mods/kenshi-online/ (standard mod location)
    if not exist "%KENSHI_DIR%\mods\kenshi-online" mkdir "%KENSHI_DIR%\mods\kenshi-online"
    copy /Y "%~dp0kenshi-online.mod" "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod" >nul
    echo         Copied kenshi-online.mod to mods/

    :: Add to __mods.list if not present
    findstr /C:"kenshi-online" "%KENSHI_DIR%\data\__mods.list" >nul 2>&1
    if errorlevel 1 (
        echo kenshi-online>> "%KENSHI_DIR%\data\__mods.list"
        echo         Added kenshi-online to mod load list
    ) else (
        echo         kenshi-online already in mod load list
    )
) else (
    echo         [INFO] kenshi-online.mod not in package (mod template spawning disabled)
)

:: ── Copy Server ──
echo  [7/7] Installing dedicated server...

if exist "%~dp0KenshiMP.Server.exe" (
    copy /Y "%~dp0KenshiMP.Server.exe" "%KENSHI_DIR%\KenshiMP.Server.exe" >nul
    echo         Copied KenshiMP.Server.exe
) else (
    echo         [INFO] KenshiMP.Server.exe not in package (hosting optional)
)

:: ── Done ──
echo.
echo  ============================================
echo   Installation complete!
echo  ============================================
echo.
echo   TO JOIN A GAME:
echo   1. Launch Kenshi normally
echo   2. Click MULTIPLAYER on the main menu
echo   3. Click JOIN GAME, enter the host's IP
echo   4. Click CONNECT, then click NEW GAME
echo   5. You'll auto-connect when the world loads!
echo.
echo   TO HOST A GAME:
echo   1. Launch Kenshi normally
echo   2. Click MULTIPLAYER on the main menu
echo   3. Click HOST GAME (starts the server)
echo   4. Port 27800 is auto-forwarded via UPnP
echo   5. Share your IP with friends!
echo   6. Click NEW GAME to start playing
echo.
echo   Default port: 27800
echo   Backups saved to: %BACKUP_DIR%
echo   To uninstall, run uninstall.bat
echo.
pause
