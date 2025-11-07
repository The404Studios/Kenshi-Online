@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Kenshi Online - Update Project Files
echo ========================================
echo.

set VCXPROJ=%~dp0KenshiOnlineMod\KenshiOnlineMod.vcxproj
set DXHOOK=%~dp0KenshiOnlineMod\DirectX11Hook.cpp

if not exist "%VCXPROJ%" (
    echo ERROR: KenshiOnlineMod.vcxproj not found!
    pause
    exit /b 1
)

echo [1/3] Backing up project file...
copy /Y "%VCXPROJ%" "%VCXPROJ%.backup" >nul

echo [2/3] Updating vcxproj with ImGui files...

REM The vcxproj update will be done via PowerShell for easier XML manipulation
powershell -Command "& { ^
    [xml]$xml = Get-Content '%VCXPROJ%'; ^
    $ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable); ^
    $ns.AddNamespace('ms', 'http://schemas.microsoft.com/developer/msbuild/2003'); ^
    $compileGroup = $xml.SelectSingleNode('//ms:ItemGroup[ms:ClCompile]', $ns); ^
    $includeGroup = $xml.SelectSingleNode('//ms:ItemGroup[ms:ClInclude]', $ns); ^
    ^
    $newFiles = @( ^
        'imgui\imgui_draw.cpp', ^
        'imgui\imgui_tables.cpp', ^
        'imgui\imgui_widgets.cpp', ^
        'imgui\backends\imgui_impl_dx11.cpp', ^
        'imgui\backends\imgui_impl_win32.cpp' ^
    ); ^
    ^
    foreach ($file in $newFiles) { ^
        $existing = $compileGroup.SelectSingleNode(\"ms:ClCompile[@Include='$file']\", $ns); ^
        if (-not $existing) { ^
            $elem = $xml.CreateElement('ClCompile', $xml.DocumentElement.NamespaceURI); ^
            $elem.SetAttribute('Include', $file); ^
            $compileGroup.AppendChild($elem) | Out-Null; ^
        } ^
    } ^
    ^
    $newHeaders = @( ^
        'imgui\imgui_internal.h', ^
        'imgui\imconfig.h', ^
        'imgui\backends\imgui_impl_dx11.h', ^
        'imgui\backends\imgui_impl_win32.h' ^
    ); ^
    ^
    foreach ($file in $newHeaders) { ^
        $existing = $includeGroup.SelectSingleNode(\"ms:ClInclude[@Include='$file']\", $ns); ^
        if (-not $existing) { ^
            $elem = $xml.CreateElement('ClInclude', $xml.DocumentElement.NamespaceURI); ^
            $elem.SetAttribute('Include', $file); ^
            $includeGroup.AppendChild($elem) | Out-Null; ^
        } ^
    } ^
    ^
    $xml.Save('%VCXPROJ%'); ^
}"

echo [3/3] Updating DirectX11Hook.cpp to use real backends...

REM Update the #include statements in DirectX11Hook.cpp
powershell -Command "& { ^
    $content = Get-Content '%DXHOOK%' -Raw; ^
    $content = $content -replace '#include \"imgui/imgui.h\"', '#include \"imgui/imgui.h\"`n#include \"imgui/backends/imgui_impl_dx11.h\"`n#include \"imgui/backends/imgui_impl_win32.h\"'; ^
    $content = $content -replace 'namespace ImGui\s*\{[^}]*ImGui_ImplDX11_Init[^}]*\}', '// Using official ImGui backends now'; ^
    $content = $content -replace 'namespace ImGui\s*\{[^}]*ImGui_ImplWin32_Init[^}]*\}', ''; ^
    Set-Content '%DXHOOK%' -Value $content -NoNewline; ^
}"

echo.
echo ========================================
echo Project Update Complete!
echo ========================================
echo.
echo Changes made:
echo   - Added ImGui source files to vcxproj
echo   - Updated DirectX11Hook.cpp includes
echo   - Backup saved as .backup
echo.
echo Next Steps:
echo   1. Open Visual Studio
echo   2. Reload the solution
echo   3. Build the project (Release/x64)
echo.
pause
