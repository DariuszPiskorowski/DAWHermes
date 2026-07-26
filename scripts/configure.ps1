Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = Get-BuildDir -RepoRoot $repoRoot

$cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')
$msvc = Get-MsvcInfo

Write-Host "Repository root: $repoRoot"
Write-Host "Build directory: $buildDir"
Write-Host "CMake: $($cmake.Path) ($($cmake.Version))"
Write-Host "MSVC: $($msvc.InstallationPath) ($($msvc.InstallationVersion))"
Write-Host "Generator: $($msvc.Generator)"

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

& $cmake.Path -S $repoRoot -B $buildDir -G $msvc.Generator -A x64

Write-Host 'Configuration completed successfully.'
