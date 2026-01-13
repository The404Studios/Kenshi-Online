# KenshiOnlineMod Dependency Setup Script (PowerShell)

$ErrorActionPreference = "Stop"

Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "   KenshiOnlineMod Dependency Setup" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# Check if git is available
try {
    $null = Get-Command git -ErrorAction Stop
} catch {
    Write-Host "ERROR: Git is not installed or not in PATH." -ForegroundColor Red
    Write-Host "Please install Git from https://git-scm.com/"
    Read-Host "Press Enter to exit"
    exit 1
}

# Create vendor directory
$vendorPath = Join-Path $PSScriptRoot "vendor"
if (-not (Test-Path $vendorPath)) {
    Write-Host "Creating vendor directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $vendorPath | Out-Null
}

Push-Location $vendorPath

# Setup ImGui
Write-Host ""
Write-Host "[1/2] Setting up ImGui..." -ForegroundColor Green
$imguiPath = Join-Path $vendorPath "imgui"
if (Test-Path $imguiPath) {
    Write-Host "ImGui already exists, updating..." -ForegroundColor Yellow
    Push-Location $imguiPath
    git fetch origin
    git checkout v1.90.1 2>&1 | Out-Null
    Pop-Location
} else {
    Write-Host "Cloning ImGui v1.90.1..." -ForegroundColor Yellow
    git clone --depth 1 --branch v1.90.1 https://github.com/ocornut/imgui.git imgui
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to setup ImGui" -ForegroundColor Red
    Pop-Location
    Read-Host "Press Enter to exit"
    exit 1
}

# Setup MinHook
Write-Host ""
Write-Host "[2/2] Setting up MinHook..." -ForegroundColor Green
$minhookPath = Join-Path $vendorPath "minhook"
if (Test-Path $minhookPath) {
    Write-Host "MinHook already exists, updating..." -ForegroundColor Yellow
    Push-Location $minhookPath
    git fetch origin
    git checkout v1.3.3 2>&1 | Out-Null
    Pop-Location
} else {
    Write-Host "Cloning MinHook v1.3.3..." -ForegroundColor Yellow
    git clone --depth 1 --branch v1.3.3 https://github.com/TsudaKageyu/minhook.git minhook
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to setup MinHook" -ForegroundColor Red
    Pop-Location
    Read-Host "Press Enter to exit"
    exit 1
}

Pop-Location

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "   Setup Complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Dependencies have been installed to the vendor folder."
Write-Host "You can now open KenshiOnlineMod.sln in Visual Studio."
Write-Host ""
Write-Host "Build configurations available:"
Write-Host "  - Debug   x86/x64"
Write-Host "  - Release x86/x64"
Write-Host ""
Write-Host "Note: Kenshi uses x64, so build with x64 configuration." -ForegroundColor Yellow
Write-Host ""

Read-Host "Press Enter to continue"
