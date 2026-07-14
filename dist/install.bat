@echo off
setlocal EnableExtensions DisableDelayedExpansion
title KenshiMP Installer
color 0A

set "EXIT_CODE=0"
set "QUIET=0"
set "FIRST_INSTALL=0"
if /I "%~2"=="/quiet" set "QUIET=1"

echo.
echo  ============================================
echo   KenshiMP - Kenshi Multiplayer Mod
echo   Safe Windows Installer
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
    echo          Select the Kenshi installation directory, not a subdirectory.
    goto :invalid_kenshi
)
if not exist "%KENSHI_DIR%\data\gui\layout" (
    echo  [ERROR] Required Kenshi layout directory is missing:
    echo          "%KENSHI_DIR%\data\gui\layout"
    goto :invalid_kenshi
)
if not exist "%KENSHI_DIR%\Plugins_x64.cfg" (
    echo  [ERROR] Required Kenshi file is missing: Plugins_x64.cfg
    goto :invalid_kenshi
)
if not exist "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" (
    echo  [ERROR] Required Kenshi file is missing: Kenshi_MainMenu.layout
    goto :invalid_kenshi
)
if not exist "%KENSHI_DIR%\mods" (
    echo  [ERROR] Required Kenshi mods directory is missing:
    echo          "%KENSHI_DIR%\mods"
    goto :invalid_kenshi
)

echo  Found Kenshi at: "%KENSHI_DIR%"
echo.

echo  [1/6] Validating release package...
call :require_file "%~dp0KenshiMP.Core.dll" "KenshiMP.Core.dll" 65536
if errorlevel 1 goto :invalid_package
call :require_file "%~dp0KenshiMP.Server.exe" "KenshiMP.Server.exe" 1
if errorlevel 1 goto :invalid_package
call :require_file "%~dp0Kenshi_MainMenu.layout" "Kenshi_MainMenu.layout" 1
if errorlevel 1 goto :invalid_package
call :require_file "%~dp0Kenshi_MultiplayerHUD.layout" "Kenshi_MultiplayerHUD.layout" 1
if errorlevel 1 goto :invalid_package
call :require_file "%~dp0Kenshi_MultiplayerPanel.layout" "Kenshi_MultiplayerPanel.layout" 1
if errorlevel 1 goto :invalid_package
call :require_file "%~dp0kenshi-online.mod" "kenshi-online.mod" 1
if errorlevel 1 goto :invalid_package
call :require_file "%~dp0server.json" "server.json" 1
if errorlevel 1 goto :invalid_package
echo         All required package files are present.

tasklist /FI "IMAGENAME eq kenshi_x64.exe" 2>NUL | find /I "kenshi_x64.exe" >NUL
if not errorlevel 1 (
    echo  [ERROR] Kenshi is running. Close it before installing.
    set "EXIT_CODE=3"
    goto :finish
)

echo  [2/6] Checking write access...
call :probe_directory "%KENSHI_DIR%"
if errorlevel 1 goto :permission_error
call :probe_directory "%KENSHI_DIR%\data"
if errorlevel 1 goto :permission_error
call :probe_directory "%KENSHI_DIR%\data\gui\layout"
if errorlevel 1 goto :permission_error
call :probe_directory "%KENSHI_DIR%\mods"
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
echo         Required locations are writable.

set "STATE_DIR=%KENSHI_DIR%\.KenshiMP-install-state"
if exist "%STATE_DIR%\installed.marker" (
    findstr /X /C:"KenshiMP managed installation v1" "%STATE_DIR%\installed.marker" >NUL 2>&1
    if errorlevel 1 goto :unsafe_state
    call :validate_state
    if errorlevel 1 goto :unsafe_state
    set "FIRST_INSTALL=0"
    echo  [3/6] Existing managed installation found; preserving original backups.
    goto :install_files
)
if exist "%STATE_DIR%" (
    echo  [ERROR] Unsafe or incomplete installer state exists:
    echo          "%STATE_DIR%"
    echo          Move it aside after inspecting it, then run the installer again.
    set "EXIT_CODE=4"
    goto :finish
)

echo  [3/6] Recording original files...
mkdir "%STATE_DIR%" >NUL 2>&1
if errorlevel 1 goto :permission_error
>"%STATE_DIR%\transaction.marker" echo KenshiMP installer transaction v1
if not exist "%STATE_DIR%\transaction.marker" goto :permission_error
set "FIRST_INSTALL=1"

call :capture_original "%KENSHI_DIR%\KenshiMP.Core.dll" "core"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\KenshiMP.Server.exe" "server"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\server.json" "server-config"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\Plugins_x64.cfg" "plugins-config"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" "main-menu"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout" "panel-layout"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout" "hud-layout"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\data\kenshi-online.mod" "data-mod"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod" "folder-mod"
if errorlevel 1 goto :install_failed
call :capture_original "%KENSHI_DIR%\data\__mods.list" "mods-list"
if errorlevel 1 goto :install_failed

:install_files
echo  [4/6] Installing package-owned files...
if not exist "%KENSHI_DIR%\mods\kenshi-online" mkdir "%KENSHI_DIR%\mods\kenshi-online" >NUL 2>&1
if not exist "%KENSHI_DIR%\mods\kenshi-online" goto :install_failed

call :copy_required "%~dp0KenshiMP.Core.dll" "%KENSHI_DIR%\KenshiMP.Core.dll"
if errorlevel 1 goto :install_failed
call :copy_required "%~dp0KenshiMP.Server.exe" "%KENSHI_DIR%\KenshiMP.Server.exe"
if errorlevel 1 goto :install_failed
call :copy_required "%~dp0server.json" "%KENSHI_DIR%\server.json"
if errorlevel 1 goto :install_failed
call :copy_required "%~dp0Kenshi_MainMenu.layout" "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout"
if errorlevel 1 goto :install_failed
call :copy_required "%~dp0Kenshi_MultiplayerPanel.layout" "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout"
if errorlevel 1 goto :install_failed
call :copy_required "%~dp0Kenshi_MultiplayerHUD.layout" "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout"
if errorlevel 1 goto :install_failed
call :copy_required "%~dp0kenshi-online.mod" "%KENSHI_DIR%\data\kenshi-online.mod"
if errorlevel 1 goto :install_failed
call :copy_required "%~dp0kenshi-online.mod" "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod"
if errorlevel 1 goto :install_failed

echo  [5/6] Updating Kenshi configuration...
set "KMP_CONFIG_FILE=%KENSHI_DIR%\Plugins_x64.cfg"
set "KMP_CONFIG_ENTRY=Plugin=KenshiMP.Core"
call :ensure_config_entry
if errorlevel 1 goto :install_failed
set "KMP_CONFIG_FILE=%KENSHI_DIR%\data\__mods.list"
set "KMP_CONFIG_ENTRY=kenshi-online"
call :ensure_config_entry
if errorlevel 1 goto :install_failed

echo  [6/6] Verifying installation...
for %%F in (
    "%KENSHI_DIR%\KenshiMP.Core.dll"
    "%KENSHI_DIR%\KenshiMP.Server.exe"
    "%KENSHI_DIR%\server.json"
    "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout"
    "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout"
    "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout"
    "%KENSHI_DIR%\data\kenshi-online.mod"
    "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod"
) do if not exist "%%~fF" goto :verification_failed
findstr /X /C:"Plugin=KenshiMP.Core" "%KENSHI_DIR%\Plugins_x64.cfg" >NUL 2>&1
if errorlevel 1 goto :verification_failed
findstr /X /C:"kenshi-online" "%KENSHI_DIR%\data\__mods.list" >NUL 2>&1
if errorlevel 1 goto :verification_failed

if "%FIRST_INSTALL%"=="1" (
    >"%STATE_DIR%\installed.marker" echo KenshiMP managed installation v1
    if not exist "%STATE_DIR%\installed.marker" goto :install_failed
    del /F /Q "%STATE_DIR%\transaction.marker" >NUL 2>&1
)

echo.
echo  [OK] KenshiMP installation completed and verified.
echo       Original files are recorded in:
echo       "%STATE_DIR%"
set "EXIT_CODE=0"
goto :finish

:invalid_kenshi
set "EXIT_CODE=1"
goto :finish

:invalid_package
echo  [ERROR] The release package is incomplete or contains an invalid file.
echo          Extract the complete KenshiMP-Install.zip and try again.
set "EXIT_CODE=2"
goto :finish

:permission_error
echo  [ERROR] Kenshi files are not writable.
echo          Run this installer with permission to modify the Kenshi directory.
set "EXIT_CODE=3"
goto :finish

:unsafe_state
echo  [ERROR] KenshiMP installer state is incomplete or unrecognized:
echo          "%STATE_DIR%"
echo          No Kenshi files were changed. Inspect or move the state directory.
set "EXIT_CODE=4"
goto :finish

:verification_failed
echo  [ERROR] Post-install verification failed.
goto :install_failed

:install_failed
echo  [ERROR] Installation failed while updating Kenshi files.
if "%FIRST_INSTALL%"=="1" (
    echo          Rolling back this first installation...
    call :rollback_first_install
) else (
    echo          Original backups were preserved. Re-run the installer or uninstall safely.
)
set "EXIT_CODE=4"
goto :finish

:require_file
if not exist "%~1" (
    echo  [ERROR] Missing required package file: %~2
    exit /b 1
)
for %%F in ("%~1") do if %%~zF LSS %~3 (
    echo  [ERROR] Required package file is too small: %~2 ^(%%~zF bytes^)
    exit /b 1
)
exit /b 0

:ensure_config_entry
rem Rewrite through PowerShell so repeated installs leave one exact entry and
rem files without a trailing newline cannot concatenate the new entry.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = $env:KMP_CONFIG_FILE; $entry = $env:KMP_CONFIG_ENTRY; $lines = @(); if (Test-Path -LiteralPath $p) { $lines = @(Get-Content -LiteralPath $p) }; $lines = @($lines | Where-Object { $_ -ne $entry -and $_ -ne '' }); $lines += $entry; Set-Content -LiteralPath $p -Value $lines -Encoding ASCII" >NUL 2>&1
if errorlevel 1 exit /b 1
findstr /X /C:"%KMP_CONFIG_ENTRY%" "%KMP_CONFIG_FILE%" >NUL 2>&1
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

:capture_original
if exist "%~1" (
    copy /B /Y "%~1" "%STATE_DIR%\%~2.bak" >NUL 2>&1
    if errorlevel 1 exit /b 1
    >"%STATE_DIR%\%~2.restore" echo restore
) else (
    >"%STATE_DIR%\%~2.remove" echo remove
)
if not exist "%STATE_DIR%\%~2.restore" if not exist "%STATE_DIR%\%~2.remove" exit /b 1
exit /b 0

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

:copy_required
copy /B /Y "%~1" "%~2" >NUL 2>&1
if errorlevel 1 (
    echo  [ERROR] Could not write: "%~2"
    exit /b 1
)
exit /b 0

:rollback_first_install
set "ROLLBACK_FAILED=0"
call :restore_original "%KENSHI_DIR%\KenshiMP.Core.dll" "core"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\KenshiMP.Server.exe" "server"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\server.json" "server-config"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\Plugins_x64.cfg" "plugins-config"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MainMenu.layout" "main-menu"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerPanel.layout" "panel-layout"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\data\gui\layout\Kenshi_MultiplayerHUD.layout" "hud-layout"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\data\kenshi-online.mod" "data-mod"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\mods\kenshi-online\kenshi-online.mod" "folder-mod"
if errorlevel 1 set "ROLLBACK_FAILED=1"
call :restore_original "%KENSHI_DIR%\data\__mods.list" "mods-list"
if errorlevel 1 set "ROLLBACK_FAILED=1"
if exist "%KENSHI_DIR%\mods\kenshi-online" rmdir "%KENSHI_DIR%\mods\kenshi-online" >NUL 2>&1
if "%ROLLBACK_FAILED%"=="0" if exist "%STATE_DIR%\transaction.marker" call :remove_state_dir
if "%ROLLBACK_FAILED%"=="0" if exist "%STATE_DIR%" set "ROLLBACK_FAILED=1"
if "%ROLLBACK_FAILED%"=="1" echo  [ERROR] Rollback was incomplete; installer state was preserved for recovery.
exit /b %ROLLBACK_FAILED%

:restore_original
if exist "%STATE_DIR%\%~2.restore" (
    copy /B /Y "%STATE_DIR%\%~2.bak" "%~1" >NUL 2>&1
    exit /b %ERRORLEVEL%
)
if exist "%STATE_DIR%\%~2.remove" (
    if exist "%~1" del /F /Q "%~1" >NUL 2>&1
    if exist "%~1" exit /b 1
    exit /b 0
)
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
if not "%EXIT_CODE%"=="0" echo  Installer exit code: %EXIT_CODE%
if "%QUIET%"=="0" pause
exit /b %EXIT_CODE%
