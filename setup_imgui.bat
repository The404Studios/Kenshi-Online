@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Kenshi Online - ImGui Setup
echo ========================================
echo.

REM Set paths
set IMGUI_VERSION=v1.90.1
set IMGUI_URL=https://github.com/ocornut/imgui/archive/refs/tags/%IMGUI_VERSION%.zip
set DOWNLOAD_DIR=%~dp0temp
set IMGUI_DIR=%~dp0KenshiOnlineMod\imgui
set IMGUI_BACKENDS_DIR=%~dp0KenshiOnlineMod\imgui\backends

echo [1/5] Creating directories...
if not exist "%DOWNLOAD_DIR%" mkdir "%DOWNLOAD_DIR%"
if not exist "%IMGUI_DIR%" mkdir "%IMGUI_DIR%"
if not exist "%IMGUI_BACKENDS_DIR%" mkdir "%IMGUI_BACKENDS_DIR%"

echo [2/5] Downloading ImGui %IMGUI_VERSION%...
powershell -Command "& {[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%IMGUI_URL%' -OutFile '%DOWNLOAD_DIR%\imgui.zip'}"

if not exist "%DOWNLOAD_DIR%\imgui.zip" (
    echo ERROR: Failed to download ImGui!
    pause
    exit /b 1
)

echo [3/5] Extracting ImGui...
powershell -Command "& {Expand-Archive -Path '%DOWNLOAD_DIR%\imgui.zip' -DestinationPath '%DOWNLOAD_DIR%' -Force}"

REM Find the extracted folder (it will be imgui-VERSION)
for /d %%D in ("%DOWNLOAD_DIR%\imgui-*") do set EXTRACTED_DIR=%%D

if not exist "%EXTRACTED_DIR%" (
    echo ERROR: Failed to extract ImGui!
    pause
    exit /b 1
)

echo [4/5] Copying ImGui files...

REM Backup existing stubs
if exist "%IMGUI_DIR%\imgui.h" (
    echo Backing up existing files...
    move /Y "%IMGUI_DIR%\imgui.h" "%IMGUI_DIR%\imgui.h.stub" >nul
    move /Y "%IMGUI_DIR%\imgui.cpp" "%IMGUI_DIR%\imgui.cpp.stub" >nul
)

REM Copy core ImGui files
echo Copying core files...
copy /Y "%EXTRACTED_DIR%\imgui.h" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imgui.cpp" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imgui_draw.cpp" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imgui_tables.cpp" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imgui_widgets.cpp" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imgui_internal.h" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imconfig.h" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imstb_rectpack.h" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imstb_textedit.h" "%IMGUI_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\imstb_truetype.h" "%IMGUI_DIR%\" >nul

REM Copy DirectX 11 and Win32 backends
echo Copying DirectX 11 backend...
copy /Y "%EXTRACTED_DIR%\backends\imgui_impl_dx11.h" "%IMGUI_BACKENDS_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\backends\imgui_impl_dx11.cpp" "%IMGUI_BACKENDS_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\backends\imgui_impl_win32.h" "%IMGUI_BACKENDS_DIR%\" >nul
copy /Y "%EXTRACTED_DIR%\backends\imgui_impl_win32.cpp" "%IMGUI_BACKENDS_DIR%\" >nul

echo [5/5] Cleaning up...
rmdir /S /Q "%DOWNLOAD_DIR%"

echo.
echo ========================================
echo ImGui Setup Complete!
echo ========================================
echo.
echo The following files have been installed:
echo.
echo Core ImGui:
echo   - imgui.h, imgui.cpp
echo   - imgui_draw.cpp
echo   - imgui_tables.cpp
echo   - imgui_widgets.cpp
echo   - imgui_internal.h
echo   - imconfig.h
echo.
echo Backends:
echo   - imgui_impl_dx11.h/cpp (DirectX 11)
echo   - imgui_impl_win32.h/cpp (Windows)
echo.
echo Next Steps:
echo   1. Update KenshiOnlineMod.vcxproj to include new .cpp files
echo   2. Update DirectX11Hook.cpp to use real backends
echo   3. Rebuild the project
echo.
echo Your old stub files were backed up as *.stub
echo.
pause
