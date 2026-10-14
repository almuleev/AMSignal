param([Parameter(Mandatory=$true)][string]$InputDirectory,
      [Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$InputDirectory = [IO.Path]::GetFullPath($InputDirectory)
$caseRoot = 'dist/signing-tests-' + [Guid]::NewGuid().ToString('N')
$checks = 0
function Check([bool]$ok, [string]$message) {
    $script:checks++
    if (-not $ok) { throw $message }
}
function Reject([scriptblock]$action, [string]$message) {
    $failed = $false
    try { & $action } catch { $failed = $true }
    Check $failed $message
}
./verify_release_binaries.ps1 -InputDirectory $InputDirectory -Version $Version
Check $true 'production metadata is consistent'
Reject { ./verify_release_binaries.ps1 -InputDirectory $InputDirectory -Version $Version -RequireSignature } 'unsigned binaries must not pass signed-release verification'
Reject { ./verify_release_binaries.ps1 -InputDirectory $InputDirectory -Version '999.0.0' } 'wrong version must be rejected'
Reject { ./package_release.ps1 -InputDirectory $InputDirectory -Version '../escape' -OutputDir "$caseRoot/invalid" } 'unsafe version must be rejected'
Reject { ./package_release.ps1 -InputDirectory $InputDirectory -Version $Version -OutputDir "$caseRoot/signed" -RequireSignature } 'signed packaging must reject unsigned binaries'
Check (-not (Test-Path "$caseRoot/signed")) 'rejected signed packaging must not publish a directory'
./package_release.ps1 -InputDirectory $InputDirectory -Version $Version -OutputDir "$caseRoot/portable" -Zip
$package = [IO.Path]::GetFullPath("$caseRoot/portable")
Check (Test-Path "$package/docs/CODE_SIGNING.md") 'portable policy is included at its README link'
$entries = Get-Content -LiteralPath "$package/SHA256SUMS"
Check ($entries.Count -eq 10) 'all packaged source files have hashes'
foreach ($entry in $entries) {
    $hash, $name = $entry -split '  ', 2
    Check ((Get-FileHash -LiteralPath (Join-Path $package $name)).Hash.ToLowerInvariant() -ceq $hash) 'checksum must match final copied bytes'
}
$zip = [IO.Path]::GetFullPath("$caseRoot/AMSignal-$Version-x64.zip")
$before = (Get-FileHash -LiteralPath $zip).Hash
Reject { ./package_release.ps1 -InputDirectory $InputDirectory -Version $Version -OutputDir "$caseRoot/other" -Zip } 'existing ZIP must not be overwritten'
Check ((Get-FileHash -LiteralPath $zip).Hash -ceq $before -and -not (Test-Path "$caseRoot/other")) 'collision preserves existing ZIP and avoids partial output'
Reject { ./package_release.ps1 -InputDirectory $InputDirectory -Version $Version -OutputDir "$caseRoot/portable" } 'existing output directory must be preserved'
Reject { ./package_release.ps1 -InputDirectory $InputDirectory -Version $Version -OutputDir '../outside-signing-test' } 'output must remain inside dist'
$archiveRoot = Join-Path $caseRoot 'extracted'
Expand-Archive -LiteralPath $zip -DestinationPath $archiveRoot
Check ((Get-FileHash "$archiveRoot/lvm_reader.exe").Hash -ceq (Get-FileHash "$package/lvm_reader.exe").Hash) 'ZIP preserves executable bytes'
Check (Test-Path "$archiveRoot/docs/CODE_SIGNING.md") 'ZIP preserves the policy path'
$assets = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($zip, '.sha256'))
Check ($assets.Count -eq 3 -and $assets[2].StartsWith($before.ToLowerInvariant() + '  ')) 'release checksum includes the final ZIP'
Write-Host "$checks release signing/package checks passed. Outputs retained at $caseRoot"
