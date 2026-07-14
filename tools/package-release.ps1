[CmdletBinding()]
param(
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = Join-Path $repoRoot "build"
$releaseRoot = Join-Path $buildRoot "bin\Release"

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $buildRoot "package"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}

$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$buildRootWithSeparator = $buildRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $OutputDirectory.StartsWith($buildRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must stay inside the build directory: $buildRoot"
}

$zipPath = Join-Path $OutputDirectory "KenshiMP-Install.zip"
$manifestPath = Join-Path $OutputDirectory "package-manifest.txt"
$checksumsPath = Join-Path $OutputDirectory "SHA256SUMS.txt"
$temporaryZipPath = Join-Path $OutputDirectory ".KenshiMP-Install.zip.tmp"
$temporaryManifestPath = Join-Path $OutputDirectory ".package-manifest.txt.tmp"
$temporaryChecksumsPath = Join-Path $OutputDirectory ".SHA256SUMS.txt.tmp"

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string[]]$Lines
    )

    $content = [string]::Join("`n", $Lines) + "`n"
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $content, $encoding)
}

function Get-LowercaseSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-StreamSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream
    )

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = $sha256.ComputeHash($Stream)
        return ([System.BitConverter]::ToString($hash)).Replace("-", "").ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
}

function New-PackageInput {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$ArchivePath,

        [long]$MinimumSize = 1
    )

    return [pscustomobject]@{
        Source = $Source
        ArchivePath = $ArchivePath
        MinimumSize = $MinimumSize
    }
}

# Keep this list explicit and in ordinal ArchivePath order. The installer expects
# every packaged file at the ZIP root, so layout files must not be moved into a
# subdirectory without a separate installer change.
$packageInputs = @(
    (New-PackageInput "dist/JOINING.md" "JOINING.md"),
    (New-PackageInput "build/bin/Release/KenshiMP.Core.dll" "KenshiMP.Core.dll" 65536),
    (New-PackageInput "build/bin/Release/KenshiMP.Injector.exe" "KenshiMP.Injector.exe"),
    (New-PackageInput "build/bin/Release/KenshiMP.Server.exe" "KenshiMP.Server.exe"),
    (New-PackageInput "dist/Kenshi_MainMenu.layout" "Kenshi_MainMenu.layout"),
    (New-PackageInput "dist/Kenshi_MultiplayerHUD.layout" "Kenshi_MultiplayerHUD.layout"),
    (New-PackageInput "dist/Kenshi_MultiplayerPanel.layout" "Kenshi_MultiplayerPanel.layout"),
    (New-PackageInput "dist/install.bat" "install.bat"),
    (New-PackageInput "dist/kenshi-online.mod" "kenshi-online.mod"),
    (New-PackageInput "dist/server.json" "server.json"),
    (New-PackageInput "dist/uninstall.bat" "uninstall.bat")
)

for ($index = 1; $index -lt $packageInputs.Count; $index++) {
    $previous = $packageInputs[$index - 1].ArchivePath
    $current = $packageInputs[$index].ArchivePath
    if ([System.StringComparer]::Ordinal.Compare($previous, $current) -ge 0) {
        throw "Package inputs must remain in unique ordinal ArchivePath order: $previous, $current"
    }
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
foreach ($path in @(
    $zipPath,
    $manifestPath,
    $checksumsPath,
    $temporaryZipPath,
    $temporaryManifestPath,
    $temporaryChecksumsPath
)) {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}

$cachePath = Join-Path $buildRoot "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw "Release build is not configured. Run cmake --preset x64-release first."
}

Write-Host "Building the configured Release tree before packaging..."
Push-Location $repoRoot
try {
    & cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

$validatedInputs = @()
$repoRootWithSeparator = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
foreach ($input in $packageInputs) {
    $sourcePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $input.Source))
    if (-not $sourcePath.StartsWith($repoRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Package input resolves outside the repository: $($input.Source)"
    }

    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Required package input is missing: $($input.Source)"
    }

    $file = Get-Item -LiteralPath $sourcePath
    if ($file.Length -lt $input.MinimumSize) {
        throw "Required package input is too small: $($input.Source) is $($file.Length) bytes; minimum is $($input.MinimumSize)."
    }

    $validatedInputs += [pscustomobject]@{
        ArchivePath = $input.ArchivePath
        FullPath = $sourcePath
        Size = $file.Length
        Sha256 = Get-LowercaseSha256 $sourcePath
    }
}

$manifestLines = @(
    "# KenshiMP deterministic Windows package manifest",
    "# SHA256`tSize`tPath"
)
foreach ($input in $validatedInputs) {
    $manifestLines += "$($input.Sha256)`t$($input.Size)`t$($input.ArchivePath)"
}
Write-Utf8File -Path $temporaryManifestPath -Lines $manifestLines

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$fixedTimestamp = New-Object System.DateTimeOffset(1980, 1, 1, 0, 0, 0, [System.TimeSpan]::Zero)

$zipStream = $null
$archive = $null
try {
    $zipStream = [System.IO.File]::Open(
        $temporaryZipPath,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None
    )
    $archive = New-Object System.IO.Compression.ZipArchive(
        $zipStream,
        [System.IO.Compression.ZipArchiveMode]::Create,
        $false
    )

    foreach ($input in $validatedInputs) {
        $entry = $archive.CreateEntry(
            $input.ArchivePath,
            [System.IO.Compression.CompressionLevel]::Optimal
        )
        $entry.LastWriteTime = $fixedTimestamp

        $sourceStream = [System.IO.File]::OpenRead($input.FullPath)
        $entryStream = $entry.Open()
        try {
            $sourceStream.CopyTo($entryStream)
        } finally {
            $entryStream.Dispose()
            $sourceStream.Dispose()
        }
    }
} finally {
    if ($null -ne $archive) {
        $archive.Dispose()
    }
    if ($null -ne $zipStream) {
        $zipStream.Dispose()
    }
}

$readArchive = [System.IO.Compression.ZipFile]::OpenRead($temporaryZipPath)
try {
    $entries = @($readArchive.Entries)
    if ($entries.Count -ne $validatedInputs.Count) {
        throw "ZIP entry count does not match the manifest."
    }

    for ($index = 0; $index -lt $validatedInputs.Count; $index++) {
        $expected = $validatedInputs[$index]
        $entry = $entries[$index]
        if ($entry.FullName -cne $expected.ArchivePath) {
            throw "ZIP order mismatch at entry ${index}: $($entry.FullName), expected $($expected.ArchivePath)."
        }
        if ($entry.Length -ne $expected.Size) {
            throw "ZIP size mismatch for $($entry.FullName)."
        }

        $entryStream = $entry.Open()
        try {
            $entryHash = Get-StreamSha256 $entryStream
        } finally {
            $entryStream.Dispose()
        }
        if ($entryHash -cne $expected.Sha256) {
            throw "ZIP SHA-256 mismatch for $($entry.FullName)."
        }
    }
} finally {
    $readArchive.Dispose()
}

$zipHash = Get-LowercaseSha256 $temporaryZipPath
$manifestHash = Get-LowercaseSha256 $temporaryManifestPath
Write-Utf8File -Path $temporaryChecksumsPath -Lines @(
    "$zipHash  KenshiMP-Install.zip",
    "$manifestHash  package-manifest.txt"
)

Move-Item -LiteralPath $temporaryZipPath -Destination $zipPath
Move-Item -LiteralPath $temporaryManifestPath -Destination $manifestPath
Move-Item -LiteralPath $temporaryChecksumsPath -Destination $checksumsPath

Write-Host "Created deterministic Windows package outputs:"
Get-Item -LiteralPath $zipPath, $manifestPath, $checksumsPath |
    Select-Object FullName, Length
