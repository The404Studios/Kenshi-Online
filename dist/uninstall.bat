@echo off
setlocal EnableExtensions DisableDelayedExpansion
title KenshiMP Uninstaller
color 0C

set "EXIT_CODE=0"
set "QUIET=0"
if /I "%~2"=="/quiet" set "QUIET=1"

echo.
echo  ============================================
echo   KenshiMP - Safe Windows Uninstaller
echo  ============================================
echo.

set "KENSHI_DIR=%~1"
if defined KENSHI_DIR goto :validate_kenshi

if exist "%~dp0kenshi_x64.exe" set "KENSHI_DIR=%~dp0"
if defined KENSHI_DIR goto :validate_kenshi
if exist "%~dp0..\kenshi_x64.exe" set "KENSHI_DIR=%~dp0.."
if defined KENSHI_DIR goto :validate_kenshi
if exist "C:\Program Files (x86)\Steam\steamapps\common\Kenshi\kenshi_x64.exe" set "KENSHI_DIR=C:\Program Files (x86)\Steam\steamapps\common\Kenshi"
if defined KENSHI_DIR goto :validate_kenshi
if exist "C:\GOG Games\Kenshi\kenshi_x64.exe" set "KENSHI_DIR=C:\GOG Games\Kenshi"
if defined KENSHI_DIR goto :validate_kenshi

if "%QUIET%"=="1" (
    echo  [ERROR] Kenshi was not found and interactive prompting is disabled.
    goto :invalid_kenshi
)

echo  Kenshi could not be auto-detected.
echo  Enter the folder containing kenshi_x64.exe.
set /p "KENSHI_DIR=Path: "

:validate_kenshi
if not defined KENSHI_DIR goto :invalid_kenshi
for %%I in ("%KENSHI_DIR%") do set "KENSHI_DIR=%%~fI"
if not exist "%KENSHI_DIR%\kenshi_x64.exe" (
    echo  [ERROR] kenshi_x64.exe was not found in:
    echo          "%KENSHI_DIR%"
    goto :invalid_kenshi
)

echo  Found Kenshi at: "%KENSHI_DIR%"
echo.

tasklist /FI "IMAGENAME eq kenshi_x64.exe" 2>NUL | find /I "kenshi_x64.exe" >NUL
if not errorlevel 1 (
    echo  [ERROR] Kenshi is running. Close it before uninstalling.
    set "EXIT_CODE=3"
    goto :finish
)

set "STATE_DIR=%KENSHI_DIR%\.KenshiMP-install-state"
if not exist "%STATE_DIR%" (
    echo  [INFO] No managed installer state was found.
    echo         Checking for a legacy KenshiMP installation...
    call :legacy_uninstall
    if errorlevel 1 goto :legacy_failed
    set "EXIT_CODE=0"
    goto :finish
)
if not exist "%STATE_DIR%\installed.marker" goto :unsafe_state
findstr /X /C:"KenshiMP managed installation v1" "%STATE_DIR%\installed.marker" >NUL 2>&1
if errorlevel 1 goto :unsafe_state
call :validate_state
if errorlevel 1 goto :unsafe_state

call :probe_directory "%KENSHI_DIR%"
if errorlevel 1 goto :permission_error
call :probe_directory "%KENSHI_DIR%\data"
if errorlevel 1 goto :permission_error
call :probe_directory "%KENSHI_DIR%\data\gui\layout"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\KenshiMP.Core.dll"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\KenshiMP.Server.exe"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\server.json"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\Plugins_x64.cfg"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\data\kenshi-online.mod"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod"
if errorlevel 1 goto :permission_error
call :probe_file "%KENSHI_DIR%\data\__mods.list"
if errorlevel 1 goto :permission_error

echo  Restoring files recorded by the KenshiMP installer...
call :restore_original "%KENSHI_DIR%\KenshiMP.Core.dll" "core"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\KenshiMP.Server.exe" "server"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\server.json" "server-config"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\Plugins_x64.cfg" "plugins-config"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" "main-menu"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout" "panel-layout"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout" "hud-layout"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\data\kenshi-online.mod" "data-mod"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod" "folder-mod"
if errorlevel 1 goto :restore_failed
call :restore_original "%KENSHI_DIR%\data\__mods.list" "mods-list"
if errorlevel 1 goto :restore_failed

if exist "%KENSHI_DIR%\mods\kenshi-online" (
    rmdir "%KENSHI_DIR%\mods\kenshi-online" >NUL 2>&1
    if exist "%KENSHI_DIR%\mods\kenshi-online" (
        echo  [INFO] Preserved non-package files in mods\kenshi-online.
    )
)

call :remove_state_dir
if exist "%STATE_DIR%" goto :restore_failed

echo.
echo  [OK] KenshiMP was uninstalled. Original files were restored.
set "EXIT_CODE=0"
goto :finish

:invalid_kenshi
set "EXIT_CODE=1"
goto :finish

:permission_error
echo  [ERROR] Kenshi files are not writable.
echo          Run this uninstaller with permission to modify the Kenshi directory.
set "EXIT_CODE=3"
goto :finish

:unsafe_state
echo  [ERROR] KenshiMP installer state is incomplete or unrecognized:
echo          "%STATE_DIR%"
echo          Nothing was removed. Inspect or move the state directory.
set "EXIT_CODE=4"
goto :finish

:restore_failed
echo  [ERROR] Uninstall could not restore every recorded file.
echo          Installer state was preserved at:
echo          "%STATE_DIR%"
echo          Resolve the reported filesystem problem, then run uninstall again.
set "EXIT_CODE=4"
goto :finish

:legacy_failed
echo  [ERROR] Legacy uninstall could not safely complete every operation.
echo          Existing files were preserved wherever ownership was uncertain.
set "EXIT_CODE=4"
goto :finish

:legacy_uninstall
call :probe_directory "%KENSHI_DIR%"
if errorlevel 1 exit /b 1
call :probe_directory "%KENSHI_DIR%\data"
if errorlevel 1 exit /b 1
call :probe_directory "%KENSHI_DIR%\data\gui\layout"
if errorlevel 1 exit /b 1
call :probe_file "%KENSHI_DIR%\Plugins_x64.cfg"
if errorlevel 1 exit /b 1
call :probe_file "%KENSHI_DIR%\data\__mods.list"
if errorlevel 1 exit /b 1

rem Woki hardening: always remove exact legacy registration lines before
rem deleting binaries, so Ogre cannot reference a missing plugin.
set "KMP_CONFIG_FILE=%KENSHI_DIR%\Plugins_x64.cfg"
set "KMP_CONFIG_ENTRY=Plugin=KenshiMP.Core"
call :remove_config_entry
if errorlevel 1 exit /b 1
set "KMP_CONFIG_FILE=%KENSHI_DIR%\data\__mods.list"
set "KMP_CONFIG_ENTRY=kenshi-online"
call :remove_config_entry
if errorlevel 1 exit /b 1

call :delete_if_exists "%KENSHI_DIR%\KenshiMP.Core.dll"
if errorlevel 1 exit /b 1
call :delete_if_exists "%KENSHI_DIR%\KenshiMP.Server.exe"
if errorlevel 1 exit /b 1
call :delete_if_exists "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout"
if errorlevel 1 exit /b 1
call :delete_if_exists "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout"
if errorlevel 1 exit /b 1
call :delete_if_exists "%KENSHI_DIR%\data\kenshi-online.mod"
if errorlevel 1 exit /b 1
call :delete_if_exists "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod"
if errorlevel 1 exit /b 1

set "LEGACY_BACKUP_DIR=%KENSHI_DIR%\KenshiMP_backup"
if exist "%LEGACY_BACKUP_DIR%\Plugins_x64.cfg.bak" (
    copy /B /Y "%LEGACY_BACKUP_DIR%\Plugins_x64.cfg.bak" "%KENSHI_DIR%\Plugins_x64.cfg" >NUL 2>&1
    if errorlevel 1 exit /b 1
    del /F /Q "%LEGACY_BACKUP_DIR%\Plugins_x64.cfg.bak" >NUL 2>&1
)
if exist "%LEGACY_BACKUP_DIR%\Kenshi_MainMenu.layout.bak" (
    copy /B /Y "%LEGACY_BACKUP_DIR%\Kenshi_MainMenu.layout.bak" "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" >NUL 2>&1
    if errorlevel 1 exit /b 1
    del /F /Q "%LEGACY_BACKUP_DIR%\Kenshi_MainMenu.layout.bak" >NUL 2>&1
) else (
    call :strip_legacy_menu_button
    if errorlevel 1 exit /b 1
)
if exist "%LEGACY_BACKUP_DIR%\__mods.list.bak" (
    copy /B /Y "%LEGACY_BACKUP_DIR%\__mods.list.bak" "%KENSHI_DIR%\data\__mods.list" >NUL 2>&1
    if errorlevel 1 exit /b 1
    del /F /Q "%LEGACY_BACKUP_DIR%\__mods.list.bak" >NUL 2>&1
)

if exist "%KENSHI_DIR%\mods\kenshi-online" (
    rmdir "%KENSHI_DIR%\mods\kenshi-online" >NUL 2>&1
    if exist "%KENSHI_DIR%\mods\kenshi-online" echo  [INFO] Preserved non-package files in mods\kenshi-online.
)
if exist "%LEGACY_BACKUP_DIR%" (
    rmdir "%LEGACY_BACKUP_DIR%" >NUL 2>&1
    if exist "%LEGACY_BACKUP_DIR%" echo  [INFO] Preserved unrecognized files in KenshiMP_backup.
)

echo  [OK] Legacy KenshiMP files and registration entries were removed.
exit /b 0

:remove_config_entry
if not exist "%KMP_CONFIG_FILE%" exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = $env:KMP_CONFIG_FILE; $entry = $env:KMP_CONFIG_ENTRY; $lines = @(Get-Content -LiteralPath $p) | Where-Object { $_ -ne $entry }; $text = if ($lines.Count -gt 0) { ($lines -join \"`r`n\") + \"`r`n\" } else { '' }; [IO.File]::WriteAllText($p, $text, [Text.Encoding]::ASCII)" >NUL 2>&1
exit /b %ERRORLEVEL%

:delete_if_exists
if not exist "%~1" exit /b 0
del /F /Q "%~1" >NUL 2>&1
if exist "%~1" exit /b 1
exit /b 0

:strip_legacy_menu_button
if not exist "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" exit /b 0
set "KMP_LEGACY_LAYOUT=%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = $env:KMP_LEGACY_LAYOUT; $c = Get-Content -LiteralPath $p -Raw; $c = [Text.RegularExpressions.Regex]::Replace($c, '(?s)\s*<Widget[^>]*name=\"MultiplayerButton\"[^>]*>.*?</Widget>', ''); [IO.File]::WriteAllText($p, $c, [Text.UTF8Encoding]::new($false))" >NUL 2>&1
exit /b %ERRORLEVEL%

:probe_directory
set "PROBE_FILE=%~1\.kenshimp-write-test-%RANDOM%-%RANDOM%.tmp"
>"%PROBE_FILE%" echo write-test
if not exist "%PROBE_FILE%" exit /b 1
del /F /Q "%PROBE_FILE%" >NUL 2>&1
if exist "%PROBE_FILE%" exit /b 1
exit /b 0

:probe_file
if not exist "%~1" exit /b 0
set "KMP_PROBE_FILE=%~1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $stream = [IO.File]::Open($env:KMP_PROBE_FILE, 'Open', 'Write', 'ReadWrite'); $stream.Dispose(); exit 0 } catch { exit 1 }" >NUL 2>&1
exit /b %ERRORLEVEL%

:restore_original
if exist "%STATE_DIR%\%~2.restore" (
    if not exist "%STATE_DIR%\%~2.bak" (
        echo  [ERROR] Missing backup for %~2.
        exit /b 1
    )
    copy /B /Y "%STATE_DIR%\%~2.bak" "%~1" >NUL 2>&1
    if errorlevel 1 (
        echo  [ERROR] Could not restore: "%~1"
        exit /b 1
    )
    echo  [OK] Restored "%~1"
    exit /b 0
)
if exist "%STATE_DIR%\%~2.remove" (
    if exist "%~1" del /F /Q "%~1" >NUL 2>&1
    if exist "%~1" (
        echo  [ERROR] Could not remove: "%~1"
        exit /b 1
    )
    echo  [OK] Removed package-owned "%~1"
    exit /b 0
)
echo  [ERROR] Installer state is incomplete for %~2.
exit /b 1

:validate_state
for %%K in (core server server-config plugins-config main-menu panel-layout hud-layout data-mod folder-mod mods-list) do (
    call :validate_state_entry "%%K"
    if errorlevel 1 exit /b 1
)
exit /b 0

:validate_state_entry
if exist "%STATE_DIR%\%~1.restore" (
    if exist "%STATE_DIR%\%~1.remove" exit /b 1
    if not exist "%STATE_DIR%\%~1.bak" exit /b 1
    exit /b 0
)
if exist "%STATE_DIR%\%~1.remove" exit /b 0
exit /b 1

:remove_state_dir
for %%K in (core server server-config plugins-config main-menu panel-layout hud-layout data-mod folder-mod mods-list) do (
    if exist "%STATE_DIR%\%%K.bak" del /F /Q "%STATE_DIR%\%%K.bak" >NUL 2>&1
    if exist "%STATE_DIR%\%%K.restore" del /F /Q "%STATE_DIR%\%%K.restore" >NUL 2>&1
    if exist "%STATE_DIR%\%%K.remove" del /F /Q "%STATE_DIR%\%%K.remove" >NUL 2>&1
)
if exist "%STATE_DIR%\installed.marker" del /F /Q "%STATE_DIR%\installed.marker" >NUL 2>&1
if exist "%STATE_DIR%\transaction.marker" del /F /Q "%STATE_DIR%\transaction.marker" >NUL 2>&1
rmdir "%STATE_DIR%" >NUL 2>&1
exit /b 0

:finish
echo.
if not "%EXIT_CODE%"=="0" echo  Uninstaller exit code: %EXIT_CODE%
if "%QUIET%"=="0" pause
exit /b %EXIT_CODE%
