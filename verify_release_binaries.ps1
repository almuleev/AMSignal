param(
    [Parameter(Mandatory=$true)][string]$InputDirectory,
    [Parameter(Mandatory=$true)][string]$Version,
    [switch]$RequireSignature
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9.+-]{0,127}$') { throw 'Invalid release version.' }
$expected = @("AMSignal-$Version-x64.exe", 'lvm_reader.exe')
$publisher = $null
foreach ($name in $expected) {
    $path = Join-Path $InputDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required executable not found: $name" }
    $item = Get-Item -LiteralPath $path
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Executable must not be a directory link.' }
    $info = $item.VersionInfo
    if ($info.ProductName -cne 'AMSignal' -or $info.ProductVersion -cne $Version -or
        $info.FileVersion -cne $Version -or $info.OriginalFilename -cne $name) {
        throw "Executable metadata does not match release $Version`: $name"
    }
    if ($RequireSignature) {
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or -not $signature.TimeStamperCertificate) {
            throw "Trusted, timestamped Authenticode signature is required: $name ($($signature.Status))"
        }
        if ($signature.SignerCertificate.Subject -notmatch '(?:^|,\s*)CN=SignPath Foundation(?:,|$)') {
            throw "Unexpected signing publisher: $name"
        }
        $thumbprint = $signature.SignerCertificate.Thumbprint
        if ($publisher -and $publisher -cne $thumbprint) { throw 'GUI and CLI must have the same signing certificate.' }
        $publisher = $thumbprint
    }
}
Write-Host "Verified release metadata$(if ($RequireSignature) { ', signatures and timestamps' }): $Version"
