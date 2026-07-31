[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-ci',

    [string]$OutputDirectory = 'ci-artifacts\DAWHermes-Windows-x64-Release',

    [string]$LogDirectory = 'ci-logs',

    [string]$SafetyRoot = '',

    [string]$CommitSha = '',

    [string]$RefName = '',

    [string]$WorkflowRunId = '',

    [string]$RunnerLabel = 'windows-2022',

    [string]$PythonVersion = '',

    [string]$CMakeVersion = '',

    [string]$MsvcVersion = '',

    [ValidateSet('passed', 'failed')]
    [string]$DeterministicTestResult = 'passed',

    [ValidateSet('skipped', 'passed')]
    [string]$EmbeddedHermesIntegrationStatus = 'skipped'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$allowedRoot = if ([string]::IsNullOrWhiteSpace($SafetyRoot)) {
    $repoRoot
} else {
    [System.IO.Path]::GetFullPath($SafetyRoot)
}
$buildDir = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $BuildDirectory
$outputDir = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $OutputDirectory
$logDir = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $LogDirectory
[void](Assert-PathWithinRoot -Path $buildDir -Root $allowedRoot)
[void](Assert-PathWithinRoot -Path $outputDir -Root $allowedRoot)
[void](Assert-PathWithinRoot -Path $logDir -Root $allowedRoot)

$sourceExecutable = Get-BuiltExecutablePath -BuildDir $buildDir -Configuration Release
Assert-WindowsX64Pe -Path $sourceExecutable | Out-Null
$runtimeSourceDirectory = Split-Path -Parent $sourceExecutable

if (Test-Path -LiteralPath $outputDir) {
    Remove-Item -LiteralPath $outputDir -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $outputDir 'app') -Force | Out-Null
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$omittedExtensions = @('.pdb', '.ilk')
$forbiddenExtensions = @(
    '.obj', '.lib', '.exp', '.vcxproj', '.filters', '.cache', '.log',
    '.mid', '.midi', '.wav', '.vst3', '.preset', '.lic', '.license'
)
$forbiddenNamePatterns = @('dead.?man', 'plugin.?catalog', 'cmakecache')

$sourceFiles = @(Get-ChildItem -LiteralPath $runtimeSourceDirectory -Recurse -File)
foreach ($sourceFile in $sourceFiles) {
    $extension = $sourceFile.Extension.ToLowerInvariant()
    if ($omittedExtensions -contains $extension) {
        continue
    }
    if ($forbiddenExtensions -contains $extension) {
        throw "Forbidden file found in the runtime source directory: $($sourceFile.FullName)"
    }
    foreach ($pattern in $forbiddenNamePatterns) {
        if ($sourceFile.Name -match $pattern) {
            throw "Forbidden runtime file name found: $($sourceFile.FullName)"
        }
    }

    $relative = Get-PathRelativeToRoot -Path $sourceFile.FullName -Root $runtimeSourceDirectory
    $destination = Join-Path (Join-Path $outputDir 'app') $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile.FullName -Destination $destination -Force
}

$packagedExecutable = Join-Path $outputDir 'app\DAWHermes.exe'
if (-not (Test-Path -LiteralPath $packagedExecutable -PathType Leaf)) {
    throw "Packaged executable missing: $packagedExecutable"
}
Assert-WindowsX64Pe -Path $packagedExecutable | Out-Null

$installerSource = Join-Path $PSScriptRoot 'install-release-artifact.ps1'
Copy-Item -LiteralPath $installerSource `
    -Destination (Join-Path $outputDir 'Install-DAWHermes.ps1') `
    -Force

if ([string]::IsNullOrWhiteSpace($CommitSha)) {
    $CommitSha = (& git -C $repoRoot rev-parse HEAD).Trim()
}
if ($CommitSha -notmatch '^[0-9a-fA-F]{40}$') {
    throw "Invalid exact commit SHA for BUILD-INFO.txt: '$CommitSha'"
}
if ([string]::IsNullOrWhiteSpace($RefName)) {
    $RefName = (& git -C $repoRoot branch --show-current).Trim()
}
if ([string]::IsNullOrWhiteSpace($WorkflowRunId)) {
    $WorkflowRunId = 'local-or-not-available'
}
if ([string]::IsNullOrWhiteSpace($PythonVersion)) {
    $PythonVersion = (& python --version 2>&1).ToString().Replace('Python ', '').Trim()
}
if ([string]::IsNullOrWhiteSpace($CMakeVersion)) {
    $cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')
    $CMakeVersion = $cmake.Version.ToString()
}
if ([string]::IsNullOrWhiteSpace($MsvcVersion)) {
    $MsvcVersion = (Get-MsvcInfo).InstallationVersion
}

$safeFields = @($RefName, $WorkflowRunId, $RunnerLabel, $PythonVersion, $CMakeVersion, $MsvcVersion)
foreach ($field in $safeFields) {
    if ($field -match '[\r\n]') {
        throw 'BUILD-INFO metadata fields must be single-line values.'
    }
}

$buildInfo = @(
    'Repository: DAWHermes'
    "Commit: $($CommitSha.ToLowerInvariant())"
    "Ref: $RefName"
    "Workflow run ID: $WorkflowRunId"
    "UTC build timestamp: $([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))"
    'Configuration: Release'
    'Platform: Windows x64'
    "Runner label: $RunnerLabel"
    "Python: $PythonVersion"
    "CMake: $CMakeVersion"
    "MSVC: $MsvcVersion"
    'JUCE: 8.0.13'
    'pybind11: v3.0.4'
    'DAWHERMES_ENABLE_LTCG: OFF'
    "Deterministic tests: $DeterministicTestResult"
    "Embedded Hermes integration: $EmbeddedHermesIntegrationStatus"
)
$buildInfo | Set-Content -LiteralPath (Join-Path $outputDir 'BUILD-INFO.txt') -Encoding UTF8

$manifestPath = Join-Path $outputDir 'SHA256SUMS.txt'
New-RuntimeHashManifest -ArtifactRoot $outputDir -ManifestPath $manifestPath
Assert-RuntimeHashManifest -ArtifactRoot $outputDir -ManifestPath $manifestPath

$forbiddenPackagedFiles = @(
    Get-ChildItem -LiteralPath $outputDir -Recurse -File |
        Where-Object {
            $_.Extension.ToLowerInvariant() -in @(
                '.obj', '.lib', '.exp', '.ilk', '.pdb', '.vcxproj', '.filters',
                '.mid', '.midi', '.wav', '.vst3', '.preset', '.lic', '.license', '.log'
            ) -or $_.Name -match '(?i)dead.?man|plugin.?catalog|cmakecache'
        }
)
if ($forbiddenPackagedFiles.Count -gt 0) {
    throw "Forbidden files entered the runtime artifact: $($forbiddenPackagedFiles.FullName -join ', ')"
}

$packageReport = @(
    "Artifact root: $outputDir"
    "Commit: $CommitSha"
    'Configuration: Release'
    'Platform: Windows x64 (PE AMD64 verified)'
    'DAWHERMES_ENABLE_LTCG: OFF'
    "Runtime files: $(@(Get-ChildItem -LiteralPath (Join-Path $outputDir 'app') -Recurse -File).Count)"
    'SHA-256 manifest: verified'
)
$packageReportPath = Join-Path $logDir 'package-verification.txt'
$packageReport | Set-Content -LiteralPath $packageReportPath -Encoding UTF8
$packageReport | ForEach-Object { Write-Host $_ }
