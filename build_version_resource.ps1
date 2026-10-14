# Shared Windows metadata for the two production executables. No runtime code.
function Build-VersionResource {
    param([string]$Version, [string]$OriginalFilename, [string]$OutputPath, [switch]$Cli)
    $releaseVersion = $Version -replace '^v', ''
    if ($releaseVersion -notmatch '^[A-Za-z0-9][A-Za-z0-9.+-]{0,127}$' -or
        $OriginalFilename -notmatch '^[A-Za-z0-9][A-Za-z0-9_.+-]*\.exe$') {
        throw 'Unsafe executable version or filename.'
    }
    $numbers = '0,0,0,0'
    if ($releaseVersion -match '^(\d+)\.(\d+)\.(\d+)(?:[.+-]|$)') {
        $parts = @($Matches[1], $Matches[2], $Matches[3])
        foreach ($part in $parts) { if ([uint64]$part -gt 65535) { throw 'Windows version component exceeds 65535.' } }
        $numbers = ($parts -join ',') + ',0'
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $OutputPath -Parent) | Out-Null
    $resource = if ($Cli) { 'AM_version.rc' } else { 'AM_logo.rc' }
    # Write literal C strings once; avoid quoting through PowerShell -> windres -> cpp.
    $lines = @(('#define AM_VERSION_STRING "' + $releaseVersion + '"'),
               ('#define AM_VERSION_NUMBERS ' + $numbers),
               ('#define AM_ORIGINAL_FILENAME "' + $OriginalFilename + '"'))
    if ($Cli) { $lines += '#define AM_CLI 1' }
    $lines += '#include "' + $resource + '"'
    $generated = $OutputPath + '.rc'
    [IO.File]::WriteAllLines($generated, $lines, [Text.Encoding]::ASCII)
    & windres -I $PSScriptRoot -O coff -i $generated -o $OutputPath
    if ($LASTEXITCODE -ne 0) { throw "Version resource compilation failed ($LASTEXITCODE)." }
}
