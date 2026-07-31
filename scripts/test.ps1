[CmdletBinding()]
param(
    [string]$BuildDirectory = '',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateRange(0, 64)]
    [int]$ParallelJobs = 0,

    [ValidateSet('BuildAndRun', 'BuildOnly', 'RunOnly')]
    [string]$Mode = 'BuildAndRun',

    [switch]$IncludeEmbeddedHermesIntegration
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Get-BuildDir -RepoRoot $repoRoot
} else {
    Resolve-RepositoryPath -RepoRoot $repoRoot -Path $BuildDirectory
}
$cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')
$ctestExe = Join-Path (Split-Path -Parent $cmake.Path) 'ctest.exe'

if (-not (Test-Path -LiteralPath $ctestExe -PathType Leaf)) {
    throw "ctest.exe not found next to cmake at $ctestExe"
}

if ($Mode -ne 'RunOnly') {
    if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
        $configureArguments = @('-BuildDirectory', $buildDir, '-EnableLtcg', 'OFF')
        if ($Configuration -eq 'Release') {
            $configureArguments += @('-Configuration', 'Release')
        }
        & (Join-Path $PSScriptRoot 'configure.ps1') @configureArguments
    }

    & (Join-Path $PSScriptRoot 'test-cmake-cache-parser.ps1')

    $buildArguments = @(
        '--build',
        $buildDir,
        '--config',
        $Configuration,
        '--target',
        'DAWHermesTests'
    )
    if ($ParallelJobs -gt 0) {
        $buildArguments += @(
            '--parallel',
            $ParallelJobs.ToString(),
            '--',
            "/m:$ParallelJobs",
            "/p:CL_MPCount=$ParallelJobs",
            '/p:UseMultiToolTask=false'
        )
    }

    & $cmake.Path @buildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Configuration test build failed with exit code $LASTEXITCODE"
    }
}

if ($Mode -ne 'BuildOnly') {
    if ($IncludeEmbeddedHermesIntegration) {
        Push-Location $repoRoot
        try {
            & $ctestExe --test-dir $buildDir -C $Configuration --output-on-failure
            if ($LASTEXITCODE -ne 0) {
                throw "CTest failed with exit code $LASTEXITCODE"
            }
        } finally {
            Pop-Location
        }
    } else {
        $testExecutable = Join-Path $buildDir "$Configuration\DAWHermesTests.exe"
        if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
            throw "Test executable not found at $testExecutable"
        }

        & $testExecutable --skip-embedded-hermes-integration
        if ($LASTEXITCODE -ne 0) {
            throw "Deterministic test suite failed with exit code $LASTEXITCODE"
        }
        Write-Host 'Embedded Hermes integration: skipped (environment-dependent by policy).'
    }
}

if ($Mode -eq 'BuildOnly') {
    Write-Host "$Configuration tests built successfully."
} elseif ($Mode -eq 'RunOnly') {
    Write-Host "$Configuration tests passed."
} else {
    Write-Host 'All tests passed.'
}
