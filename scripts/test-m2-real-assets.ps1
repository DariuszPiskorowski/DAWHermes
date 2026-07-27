param(
    [Parameter(Mandatory = $true)]
    [string]$BassMidi,

    [Parameter(Mandatory = $true)]
    [string]$BassWav,

    [Parameter(Mandatory = $true)]
    [string]$DrumMidi,

    [Parameter(Mandatory = $true)]
    [string]$DrumWav,

    [Parameter(Mandatory = $true)]
    [string]$SynthMidi,

    [Parameter(Mandatory = $true)]
    [string]$SynthWav,

    [string]$ReportPath,
    [string]$AuditReportPath,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

function Resolve-InputFilePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PathValue,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    $resolved = @(Resolve-Path -Path $PathValue -ErrorAction Stop)
    if ($resolved.Count -ne 1) {
        throw "Expected a single file path for $Label but received: $PathValue"
    }

    $fileInfo = Get-Item -LiteralPath $resolved[0].Path -ErrorAction Stop
    if (-not $fileInfo.Exists -or $fileInfo.PSIsContainer) {
        throw "$Label must be an existing file: $($resolved[0].Path)"
    }

    return $fileInfo.FullName
}

function Get-TestsExecutablePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $candidates = @(
        (Join-Path $BuildDir 'Release\DAWHermesTests.exe'),
        (Join-Path $BuildDir 'DAWHermesTests.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $discovered = Get-ChildItem -Path $BuildDir -Filter 'DAWHermesTests.exe' -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if ($discovered) {
        return $discovered.FullName
    }

    throw "DAWHermesTests.exe not found under $BuildDir"
}

function Get-FileHashMap {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$FilePaths
    )

    $hashes = [ordered]@{}
    foreach ($filePath in ($FilePaths | Sort-Object -Unique)) {
        $hash = Get-FileHash -LiteralPath $filePath -Algorithm SHA256
        $hashes[$filePath] = $hash.Hash.ToLowerInvariant()
    }

    return $hashes
}

function Get-CacheDirectorySnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CacheRoot
    )

    if (-not (Test-Path -LiteralPath $CacheRoot)) {
        return @()
    }

    return @(
        Get-ChildItem -Path $CacheRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { $_.FullName } |
            Sort-Object -Unique
    )
}

function Get-DownloadsDirectorySnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Directories
    )

    $snapshot = [ordered]@{}
    foreach ($directory in ($Directories | Sort-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $directory)) {
            $snapshot[$directory] = @()
            continue
        }

        $snapshot[$directory] = @(
            Get-ChildItem -Path $directory -File -ErrorAction SilentlyContinue |
                ForEach-Object { $_.FullName } |
                Sort-Object -Unique
        )
    }

    return $snapshot
}

function Resolve-OutputPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $false)]
        [string]$PathValue,

        [Parameter(Mandatory = $true)]
        [string]$DefaultRelativePath
    )

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $DefaultRelativePath))
    }

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $PathValue))
}

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = Get-BuildDir -RepoRoot $repoRoot
$cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')

$resolvedBassMidi = Resolve-InputFilePath -PathValue $BassMidi -Label 'BassMidi'
$resolvedBassWav = Resolve-InputFilePath -PathValue $BassWav -Label 'BassWav'
$resolvedDrumMidi = Resolve-InputFilePath -PathValue $DrumMidi -Label 'DrumMidi'
$resolvedDrumWav = Resolve-InputFilePath -PathValue $DrumWav -Label 'DrumWav'
$resolvedSynthMidi = Resolve-InputFilePath -PathValue $SynthMidi -Label 'SynthMidi'
$resolvedSynthWav = Resolve-InputFilePath -PathValue $SynthWav -Label 'SynthWav'

$inputFiles = @(
    $resolvedBassMidi,
    $resolvedBassWav,
    $resolvedDrumMidi,
    $resolvedDrumWav,
    $resolvedSynthMidi,
    $resolvedSynthWav
)

$reportPathResolved = Resolve-OutputPath -RepoRoot $repoRoot -PathValue $ReportPath -DefaultRelativePath 'build\m2-real-assets-report.json'
$auditReportPathResolved = Resolve-OutputPath -RepoRoot $repoRoot -PathValue $AuditReportPath -DefaultRelativePath 'build\m2-real-assets-audit.json'

$downloadsRegex = '(?i)\\downloads(\\|$)'
$inputDirectories = @($inputFiles | ForEach-Object { Split-Path -Path $_ -Parent } | Sort-Object -Unique)
$downloadsDirectories = @($inputDirectories | Where-Object { $_ -match $downloadsRegex } | Sort-Object -Unique)

$inputFileSet = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($inputFile in $inputFiles) {
    $null = $inputFileSet.Add($inputFile)
}

$preHashes = Get-FileHashMap -FilePaths $inputFiles
$preDownloadsSnapshot = Get-DownloadsDirectorySnapshot -Directories $downloadsDirectories
$cacheRoot = (Join-Path $env:LOCALAPPDATA 'DAWHermes\cache\hermes')
$cacheBefore = Get-CacheDirectorySnapshot -CacheRoot $cacheRoot

if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    & (Join-Path $PSScriptRoot 'configure.ps1')
}

if (-not $SkipBuild) {
    & $cmake.Path --build $buildDir --config Release --target DAWHermesTests
}

$testsExe = Get-TestsExecutablePath -BuildDir $buildDir

$runnerExitCode = 0
Push-Location $repoRoot
try {
    $runnerArgs = @(
        '--m2-real-assets',
        '--bass-midi', $resolvedBassMidi,
        '--bass-wav', $resolvedBassWav,
        '--drum-midi', $resolvedDrumMidi,
        '--drum-wav', $resolvedDrumWav,
        '--synth-midi', $resolvedSynthMidi,
        '--synth-wav', $resolvedSynthWav,
        '--report', $reportPathResolved
    )

    & $testsExe @runnerArgs
    $runnerExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

$postHashes = Get-FileHashMap -FilePaths $inputFiles
$postDownloadsSnapshot = Get-DownloadsDirectorySnapshot -Directories $downloadsDirectories
$cacheAfter = Get-CacheDirectorySnapshot -CacheRoot $cacheRoot

$changedInputFiles = @()
foreach ($path in $preHashes.Keys) {
    if ($preHashes[$path] -ne $postHashes[$path]) {
        $changedInputFiles += $path
    }
}

$newDownloadsFiles = @()
foreach ($downloadsDirectory in $downloadsDirectories) {
    $beforeFiles = @($preDownloadsSnapshot[$downloadsDirectory])
    $afterFiles = @($postDownloadsSnapshot[$downloadsDirectory])

    $beforeSet = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($beforeFile in $beforeFiles) {
        $null = $beforeSet.Add($beforeFile)
    }

    foreach ($candidateFile in $afterFiles) {
        if (-not $beforeSet.Contains($candidateFile) -and -not $inputFileSet.Contains($candidateFile)) {
            $newDownloadsFiles += $candidateFile
        }
    }
}
$newDownloadsFiles = @($newDownloadsFiles | Sort-Object -Unique)

$cacheBeforeSet = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($cachePath in $cacheBefore) {
    $null = $cacheBeforeSet.Add($cachePath)
}

$newCacheDirectories = @()
foreach ($cachePath in $cacheAfter) {
    if (-not $cacheBeforeSet.Contains($cachePath)) {
        $newCacheDirectories += $cachePath
    }
}
$newCacheDirectories = @($newCacheDirectories | Sort-Object -Unique)

if (-not (Test-Path -LiteralPath $reportPathResolved)) {
    throw "Expected real-assets report was not produced: $reportPathResolved"
}

$engineReport = Get-Content -LiteralPath $reportPathResolved -Raw | ConvertFrom-Json
$engineCacheNewDirectories = @()
if ($null -ne $engineReport.cache -and $null -ne $engineReport.cache.newDirectories) {
    $engineCacheNewDirectories = @($engineReport.cache.newDirectories)
}

$downloadsCheckPassed = ($newDownloadsFiles.Count -eq 0)
$fileIntegrityPassed = ($changedInputFiles.Count -eq 0)
$scriptCacheCheckPassed = ($newCacheDirectories.Count -eq 0)
$engineCacheCheckPassed = ($engineCacheNewDirectories.Count -eq 0)
$engineOverallSuccess = [bool]$engineReport.overallSuccess

$auditObject = [ordered]@{
    timestampUtc = (Get-Date).ToUniversalTime().ToString('o')
    repoRoot = $repoRoot
    runner = [ordered]@{
        executable = $testsExe
        exitCode = $runnerExitCode
        reportPath = $reportPathResolved
        overallSuccess = $engineOverallSuccess
    }
    inputs = [ordered]@{
        bassMidi = $resolvedBassMidi
        bassWav = $resolvedBassWav
        drumMidi = $resolvedDrumMidi
        drumWav = $resolvedDrumWav
        synthMidi = $resolvedSynthMidi
        synthWav = $resolvedSynthWav
    }
    fileIntegrity = [ordered]@{
        preSha256 = $preHashes
        postSha256 = $postHashes
        changedFiles = $changedInputFiles
        passed = $fileIntegrityPassed
    }
    downloadsCheck = [ordered]@{
        monitoredDirectories = $downloadsDirectories
        newFiles = $newDownloadsFiles
        passed = $downloadsCheckPassed
    }
    cacheCheck = [ordered]@{
        cacheRoot = $cacheRoot
        newDirectoriesFromScriptSnapshot = $newCacheDirectories
        newDirectoriesFromEngineReport = $engineCacheNewDirectories
        passed = ($scriptCacheCheckPassed -and $engineCacheCheckPassed)
    }
    checks = [ordered]@{
        runnerExitCodeZero = ($runnerExitCode -eq 0)
        engineOverallSuccess = $engineOverallSuccess
        inputHashesUnchanged = $fileIntegrityPassed
        noDownloadsOutputs = $downloadsCheckPassed
        cacheCleanupObserved = ($scriptCacheCheckPassed -and $engineCacheCheckPassed)
    }
    engineWarnings = @($engineReport.warnings)
}

$auditDirectory = Split-Path -Path $auditReportPathResolved -Parent
if (-not [string]::IsNullOrWhiteSpace($auditDirectory) -and -not (Test-Path -LiteralPath $auditDirectory)) {
    New-Item -Path $auditDirectory -ItemType Directory -Force | Out-Null
}

$auditObject | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $auditReportPathResolved -Encoding UTF8

Write-Host "M2 real-assets report: $reportPathResolved"
Write-Host "M2 real-assets audit: $auditReportPathResolved"
Write-Host "Bass repair: status=$($engineReport.operations.bassRepair.status) notes=$($engineReport.operations.bassRepair.generatedNoteCount)"
Write-Host "Bass sync: status=$($engineReport.operations.bassSync.status) notes=$($engineReport.operations.bassSync.generatedNoteCount)"
Write-Host "Synth sync: status=$($engineReport.operations.synthSync.status) notes=$($engineReport.operations.synthSync.generatedNoteCount)"
Write-Host "Drums extraction: status=$($engineReport.operations.drumsExtraction.status) notes=$($engineReport.operations.drumsExtraction.generatedNoteCount)"

$failures = @()
if ($runnerExitCode -ne 0) {
    $failures += "DAWHermesTests real-assets mode returned non-zero exit code: $runnerExitCode"
}
if (-not $engineOverallSuccess) {
    $failures += 'Engine report overallSuccess was false.'
}
if (-not $fileIntegrityPassed) {
    $failures += ("Input file hash changed: " + ($changedInputFiles -join '; '))
}
if (-not $downloadsCheckPassed) {
    $failures += ("Unexpected new files in monitored Downloads directories: " + ($newDownloadsFiles -join '; '))
}
if (-not $scriptCacheCheckPassed) {
    $failures += ("New Hermes cache directories detected by script snapshot: " + ($newCacheDirectories -join '; '))
}
if (-not $engineCacheCheckPassed) {
    $failures += ("Engine report detected new Hermes cache directories: " + ($engineCacheNewDirectories -join '; '))
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Error $failure
    }
    exit 1
}

Write-Host 'M2 real-assets verification checks passed.'
