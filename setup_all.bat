@echo off
echo ========================================
echo Kenshi Online - Complete Setup
echo ========================================
echo.
echo This will:
echo   1. Download ImGui library
echo   2. Update project files
echo   3. Prepare for compilation
echo.
pause

echo.
echo Running ImGui download...
call "%~dp0setup_imgui.bat"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: ImGui setup failed!
    pause
    exit /b 1
)

echo.
echo Running project update...
call "%~dp0update_project.bat"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Project update failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Setup Complete!
echo ========================================
echo.
echo ImGui has been downloaded and integrated.
echo.
echo To build:
echo   1. Open Kenshi-Online.sln in Visual Studio
echo   2. Select Release ^| x64
echo   3. Build Solution
echo.
echo To test:
echo   1. Inject KenshiOnlineMod.dll into Kenshi.exe
echo   2. Wait 5 seconds for initialization
echo   3. Login screen should appear
echo   4. Press F1 to toggle UI
echo.
pause
