#
# Kenshi Online - Build Script (Windows PowerShell)
#
# This script builds the entire project from scratch.
# Run from repository root: .\build\build.ps1
#
# Requirements:
#   - .NET 8.0 SDK
#   - CMake 3.15+
#   - Visual Studio 2022 (for C++ DLL)
#

param(
    [switch]$Clean,
    [switch]$SkipCpp,
    [switch]$SkipVerify
)

$ErrorActionPreference = "Stop"

# Configuration
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$OutputDir = Join-Path $RootDir "dist"
$CSharpProject = Join-Path $RootDir "Kenshi-Online\KenshiMultiplayer.csproj"
$CppProject = Join-Path $RootDir "KenshiOnlineMod"

# Colors
function Write-ColorOutput($ForegroundColor) {
    $fc = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $ForegroundColor
    if ($args) { Write-Output $args }
    $host.UI.RawUI.ForegroundColor = $fc
}

function Write-Header($text) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "  $text" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
}

function Write-Step($text) {
    Write-Host $text -ForegroundColor Yellow
}

function Write-Success($text) {
    Write-Host "  $text" -ForegroundColor Green
}

function Write-Warning($text) {
    Write-Host "  $text" -ForegroundColor Yellow
}

function Write-Error($text) {
    Write-Host "  $text" -ForegroundColor Red
}

# Check prerequisites
function Test-Prerequisites {
    Write-Step "Checking prerequisites..."

    # Check .NET SDK
    try {
        $dotnetVersion = & dotnet --version
        Write-Success ".NET SDK: $dotnetVersion"

        if (-not $dotnetVersion.StartsWith("8.")) {
            Write-Warning "WARNING: .NET 8.0 recommended, found $dotnetVersion"
        }
    }
    catch {
        Write-Error "ERROR: .NET SDK not found. Please install .NET 8.0 SDK."
        exit 1
    }

    # Check CMake
    try {
        $cmakeVersion = & cmake --version | Select-Object -First 1
        Write-Success "CMake: $cmakeVersion"
    }
    catch {
        Write-Warning "CMake not found (C++ DLL build will be skipped)"
        $script:SkipCpp = $true
    }

    # Check Visual Studio
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -property installationPath
        if ($vsPath) {
            Write-Success "Visual Studio: Found at $vsPath"
        }
    }
    else {
        Write-Warning "Visual Studio not found (C++ DLL may not build)"
    }

    Write-Host ""
}

# Clean previous build
function Clear-Build {
    Write-Step "Cleaning previous build..."

    # Clean .NET build
    $binPath = Join-Path $RootDir "Kenshi-Online\bin"
    $objPath = Join-Path $RootDir "Kenshi-Online\obj"

    if (Test-Path $binPath) { Remove-Item -Recurse -Force $binPath }
    if (Test-Path $objPath) { Remove-Item -Recurse -Force $objPath }

    # Clean output
    if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }

    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

    Write-Success "Clean complete"
    Write-Host ""
}

# Build C# project
function Build-CSharp {
    Write-Step "Building C# project..."

    Push-Location (Join-Path $RootDir "Kenshi-Online")

    try {
        # Restore
        Write-Host "  Restoring dependencies..."
        & dotnet restore --verbosity quiet
        if ($LASTEXITCODE -ne 0) { throw "Restore failed" }

        # Build
        Write-Host "  Building Release configuration..."
        $buildOutput = & dotnet build -c Release --no-restore 2>&1

        if ($LASTEXITCODE -ne 0) {
            Write-Error "BUILD FAILED"
            $buildOutput | ForEach-Object { Write-Host $_ }
            exit 1
        }

        # Count warnings/errors
        $warnings = ($buildOutput | Select-String "warning ").Count
        $errors = ($buildOutput | Select-String "error ").Count

        if ($errors -gt 0) {
            Write-Error "Build errors: $errors"
            $buildOutput | Select-String "error " | ForEach-Object { Write-Host $_ }
            exit 1
        }

        if ($warnings -gt 0) {
            Write-Warning "Build warnings: $warnings"
        }

        Write-Success "C# build complete"
    }
    finally {
        Pop-Location
    }

    Write-Host ""
}

# Build C++ DLL
function Build-Cpp {
    if ($SkipCpp) {
        Write-Step "Skipping C++ build (--SkipCpp or CMake not found)"
        Write-Host ""
        return
    }

    Write-Step "Building C++ DLL..."

    Push-Location $CppProject

    try {
        # Create build directory
        $buildDir = Join-Path $CppProject "build"
        if (-not (Test-Path $buildDir)) {
            New-Item -ItemType Directory -Path $buildDir | Out-Null
        }

        Push-Location $buildDir

        # Configure CMake
        Write-Host "  Configuring CMake..."
        & cmake .. -G "Visual Studio 17 2022" -A x64 2>&1 | Out-Null

        if ($LASTEXITCODE -ne 0) {
            Write-Warning "CMake configuration failed, trying older VS..."
            & cmake .. -G "Visual Studio 16 2019" -A x64 2>&1 | Out-Null
        }

        # Build
        Write-Host "  Building..."
        & cmake --build . --config Release 2>&1 | Out-Null

        $dllPath = Join-Path $buildDir "bin\Release\KenshiOnlineMod.dll"
        if (Test-Path $dllPath) {
            Write-Success "C++ DLL build complete"
        }
        else {
            Write-Warning "DLL not found at expected path"
        }

        Pop-Location
    }
    catch {
        Write-Warning "C++ build failed: $_"
    }
    finally {
        Pop-Location
    }

    Write-Host ""
}

# Package distribution
function New-Distribution {
    Write-Step "Packaging distribution..."

    # Create output directory
    if (-not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir | Out-Null
    }

    # Copy C# output
    $csharpBin = Join-Path $RootDir "Kenshi-Online\bin\Release\net8.0"

    if (Test-Path (Join-Path $csharpBin "KenshiOnline.exe")) {
        Copy-Item (Join-Path $csharpBin "KenshiOnline.exe") $OutputDir
        Write-Host "  Copied KenshiOnline.exe"
    }

    # Copy dependencies
    Get-ChildItem (Join-Path $csharpBin "*.dll") | ForEach-Object {
        Copy-Item $_.FullName $OutputDir
    }
    Write-Host "  Copied dependencies"

    # Copy C++ DLL
    $cppDll = Join-Path $CppProject "build\bin\Release\KenshiOnlineMod.dll"
    if (Test-Path $cppDll) {
        Copy-Item $cppDll $OutputDir
        Write-Host "  Copied KenshiOnlineMod.dll"
    }

    # Copy offset table
    $offsetTable = Join-Path $RootDir "offsets\offset-table.json"
    if (Test-Path $offsetTable) {
        Copy-Item $offsetTable $OutputDir
        Write-Host "  Copied offset-table.json"
    }

    # Create README
    $readme = @"
Kenshi Online - Quick Start
===========================

1. Launch Kenshi (version 1.0.64)
2. Run KenshiOnline.exe
3. Select "Host" to create a session, or "Join" to connect

Requirements:
- Kenshi version 1.0.64 (64-bit)
- Windows 10 or later
- .NET 8.0 Runtime

Troubleshooting:
- If injection fails, try running as administrator
- Ensure your antivirus is not blocking the mod
- All players must have the same Kenshi version

For more help, visit: https://github.com/The404Studios/Kenshi-Online
"@
    $readme | Out-File -FilePath (Join-Path $OutputDir "README.txt") -Encoding utf8
    Write-Host "  Created README.txt"

    Write-Success "Packaging complete"
    Write-Host ""
}

# Verify build
function Test-Build {
    if ($SkipVerify) {
        Write-Step "Skipping verification (--SkipVerify)"
        return
    }

    Write-Step "Verifying build..."

    $verifyPass = $true

    # Check executable
    $exePath = Join-Path $OutputDir "KenshiOnline.exe"
    $dllPath = Join-Path $OutputDir "KenshiOnline.dll"

    if ((Test-Path $exePath) -or (Test-Path $dllPath)) {
        Write-Success "Executable: OK"
    }
    else {
        Write-Error "Executable: MISSING"
        $verifyPass = $false
    }

    # Check dependencies
    @("Newtonsoft.Json.dll", "MemorySharp.dll") | ForEach-Object {
        $depPath = Join-Path $OutputDir $_
        if (Test-Path $depPath) {
            Write-Success "$_`: OK"
        }
        else {
            Write-Warning "$_`: Missing (may be framework-included)"
        }
    }

    # Check DLL
    $modDll = Join-Path $OutputDir "KenshiOnlineMod.dll"
    if (Test-Path $modDll) {
        # Verify architecture
        try {
            $bytes = [System.IO.File]::ReadAllBytes($modDll)
            # PE header at offset 0x3C points to PE signature
            $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
            # Machine type at PE+4
            $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)

            if ($machine -eq 0x8664) {
                Write-Success "KenshiOnlineMod.dll: OK (x64)"
            }
            elseif ($machine -eq 0x14c) {
                Write-Error "KenshiOnlineMod.dll: WRONG ARCHITECTURE (x86, needs x64)"
                $verifyPass = $false
            }
            else {
                Write-Warning "KenshiOnlineMod.dll: Unknown architecture"
            }
        }
        catch {
            Write-Warning "KenshiOnlineMod.dll: Could not verify architecture"
        }
    }
    else {
        Write-Warning "KenshiOnlineMod.dll: Missing (build on Windows required)"
    }

    Write-Host ""

    if ($verifyPass) {
        Write-Header "BUILD SUCCESSFUL"
        Write-Host "Output directory: $OutputDir"
        Write-Host ""
        Get-ChildItem $OutputDir | Format-Table Name, Length, LastWriteTime
    }
    else {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "  BUILD VERIFICATION FAILED" -ForegroundColor Red
        Write-Host "========================================" -ForegroundColor Red
        exit 1
    }
}

# Main
function Main {
    Write-Header "Kenshi Online Build Script"

    Test-Prerequisites

    if ($Clean) {
        Clear-Build
    }
    else {
        # Always clean for consistent builds
        Clear-Build
    }

    Build-CSharp
    Build-Cpp
    New-Distribution
    Test-Build
}

Main
