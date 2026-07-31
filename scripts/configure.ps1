[CmdletBinding()]
param(
    [string]$BuildDirectory = '',

    [ValidateSet('ON', 'OFF')]
    [string]$EnableLtcg = 'OFF',

    [ValidateSet('', 'Debug', 'Release')]
    [string]$Configuration = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Get-BuildDir -RepoRoot $repoRoot
} elseif ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}

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

$configureArguments = @(
    '-S',
    $repoRoot,
    '-B',
    $buildDir,
    '-G',
    $msvc.Generator,
    '-A',
    'x64'
)

$configureArguments += "-DDAWHERMES_ENABLE_LTCG=$EnableLtcg"

if (-not [string]::IsNullOrWhiteSpace($Configuration)) {
    $configureArguments += "-DCMAKE_CONFIGURATION_TYPES=$Configuration"
}

& $cmake.Path @configureArguments
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}

Write-Host 'Configuration completed successfully.'
