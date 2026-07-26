Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = Get-BuildDir -RepoRoot $repoRoot
$cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    & (Join-Path $PSScriptRoot 'configure.ps1')
}

& $cmake.Path --build $buildDir --config Release --target DAWHermes

$exePath = Get-BuiltExecutablePath -BuildDir $buildDir -Configuration Release
Write-Host "Release executable: $exePath"
