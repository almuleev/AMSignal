param(
    [string]$OutputDir = "dist\AMSignal",
    [switch]$Zip,
    [string]$InputDirectory = $PSScriptRoot,
    [string]$Version,
    [switch]$RequireSignature
)

$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

# Only create a new release directory; never recursively delete a supplied path.
$releaseRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'dist'))
$releaseTarget = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputDir))
if (-not $releaseTarget.StartsWith($releaseRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDir must be a new directory inside the project dist directory.'
}
if (Test-Path -LiteralPath $releaseTarget) {
    throw 'Output directory already exists. Choose a new OutputDir; existing files will not be removed.'
}
$ancestor = Split-Path $releaseTarget -Parent
while ($ancestor.Length -ge $releaseRoot.Length) {
    if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Release output cannot pass through a directory link.'
    }
    $ancestor = Split-Path $ancestor -Parent
}
$OutputDir = $releaseTarget

if ($Version) { $releaseVersion = $Version -replace '^v', '' }
else {
    $checkoutVersion = git -c core.excludesFile= describe --tags --always --dirty 2>$null
    if (-not $checkoutVersion) { $checkoutVersion = 'dev' }
    $releaseVersion = $checkoutVersion -replace '^v', ''
}
$InputDirectory = [IO.Path]::GetFullPath($InputDirectory)
& (Join-Path $PSScriptRoot 'verify_release_binaries.ps1') -InputDirectory $InputDirectory -Version $releaseVersion -RequireSignature:$RequireSignature
$guiExe = "AMSignal-$releaseVersion-x64.exe"
$zipName = "AMSignal-$releaseVersion-x64.zip"

$files = @(
    $guiExe,
    "lvm_reader.exe",
    "Start GUI.bat",
    "run.bat",
    "AM_logo.ico",
    "README.md",
    "README_EN.md",
    "README_RU.md",
    "LICENSE",
    "docs\CODE_SIGNING.md"
)

foreach ($file in $files) {
    $sourceRoot = if ($file.EndsWith('.exe')) { $InputDirectory } else { $PSScriptRoot }
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $file) -PathType Leaf)) {
        throw "Required file not found: $file"
    }
}
if ($Zip) {
    $zipPath = Join-Path (Split-Path $OutputDir -Parent) $zipName
    $checksumPath = [IO.Path]::ChangeExtension($zipPath, '.sha256')
    if ((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath $checksumPath)) {
        throw 'Release archive or checksum already exists; choose a new parent directory.'
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

foreach ($file in $files) {
    $sourceRoot = if ($file.EndsWith('.exe')) { $InputDirectory } else { $PSScriptRoot }
    $destination = Join-Path $OutputDir $file
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceRoot $file) -Destination $destination
}
$encoding = New-Object Text.UTF8Encoding($false)
$lines = @(Get-ChildItem -LiteralPath $OutputDir -Recurse -File | Sort-Object FullName | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' +
        $_.FullName.Substring($OutputDir.Length + 1).Replace('\', '/')
})
[IO.File]::WriteAllLines((Join-Path $OutputDir 'SHA256SUMS'), $lines, $encoding)

if ($Zip) {
    Compress-Archive -Path (Join-Path $OutputDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
    $assetLines = @($guiExe, 'lvm_reader.exe') | ForEach-Object {
        (Get-FileHash -LiteralPath (Join-Path $OutputDir $_) -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_
    }
    $assetLines += (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $zipName
    [IO.File]::WriteAllLines($checksumPath, [string[]]$assetLines, $encoding)
    Write-Host "Created $zipPath"
} else {
    Write-Host "Prepared $OutputDir"
}
