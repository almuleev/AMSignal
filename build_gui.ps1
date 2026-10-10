# Build separately compiled GUI modules; -Test links the same objects into the
# headless regression executable. Only changed sources/headers are recompiled.
param([switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path
}
$version = git -c core.excludesFile= describe --tags --always --dirty 2>$null
if (-not $version) { $version = 'dev' }
$releaseVersion = $version -replace '^v', ''
$versionDefine = '-DAPP_VERSION_W=L\"' + $version + '\"'
$compileFlags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-finput-charset=UTF-8', $versionDefine, '-I.')
$objectDir = '.build/gui'
New-Item -ItemType Directory -Force -Path $objectDir | Out-Null
$flagsPath = Join-Path $objectDir 'compile_flags.txt'
$signature = ((Get-Command g++).Source + "`n" + ($compileFlags -join "`n"))
if (-not (Test-Path -LiteralPath $flagsPath) -or [IO.File]::ReadAllText((Join-Path $PSScriptRoot $flagsPath)) -ne $signature) {
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot $flagsPath), $signature)
}
$latestDependency = (Get-ChildItem *.hpp | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
$flagsModified = (Get-Item -LiteralPath $flagsPath).LastWriteTimeUtc
if ($flagsModified -gt $latestDependency) { $latestDependency = $flagsModified }
$sources = @((Get-ChildItem gui_*.cpp | Sort-Object Name).Name) + @(
    'gap_details.cpp', 'lvm_parser.cpp', 'data_io.cpp', 'filter_engine.cpp',
    'spectrum_worker.cpp', 'fft.cpp', 'analysis.cpp', 'export_helpers.cpp', 'formula_engine.cpp',
    'frf_analysis.cpp', 'frf_stream.cpp', 'frf_worker.cpp'
)
if ($Test) { $sources += 'tests/gui_regression.cpp' }
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $objectDir (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
    $objects += $object
    $sourceModified = (Get-Item -LiteralPath $source).LastWriteTimeUtc
    $stale = -not (Test-Path -LiteralPath $object)
    if (-not $stale) {
        $objectModified = (Get-Item -LiteralPath $object).LastWriteTimeUtc
        $stale = $sourceModified -gt $objectModified -or $latestDependency -gt $objectModified
    }
    if ($stale) {
        Write-Host "Compiling $source"
        & g++ @compileFlags -c $source -o $object
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source (exit $LASTEXITCODE)." }
    }
}
$libraries = @('-lcomdlg32', '-lgdi32', '-luser32', '-lgdiplus', '-lcomctl32', '-luxtheme', '-ladvapi32', '-lshell32')
if ($Test) {
    $outName = 'tests/gui_regression.exe'
    & g++ -static -o $outName @objects @libraries
} else {
    $outName = "AMSignal-$releaseVersion-x64.exe"
    $resourceObject = Join-Path $objectDir 'AM_logo_res.o'
    & windres -O coff -i AM_logo.rc -o $resourceObject
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & g++ -municode -static -mwindows -o $outName $resourceObject @objects @libraries
}
if ($LASTEXITCODE -ne 0) { throw "Link failed: $outName (exit $LASTEXITCODE)." }
Write-Host "Built $outName" -ForegroundColor Green
if ($Test) {
    & (Join-Path $PSScriptRoot $outName)
    if ($LASTEXITCODE -ne 0) { throw 'GUI regression tests failed.' }
}
