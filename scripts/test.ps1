Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = Get-BuildDir -RepoRoot $repoRoot
$cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')
$ctestExe = Join-Path (Split-Path -Parent $cmake.Path) 'ctest.exe'

if (-not (Test-Path $ctestExe)) {
    throw "ctest.exe not found next to cmake at $ctestExe"
}

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    & (Join-Path $PSScriptRoot 'configure.ps1')
}

& $cmake.Path --build $buildDir --config Debug --target DAWHermesTests
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Push-Location $repoRoot
try {
    & $ctestExe --test-dir $buildDir -C Debug --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

Write-Host 'All tests passed.'
