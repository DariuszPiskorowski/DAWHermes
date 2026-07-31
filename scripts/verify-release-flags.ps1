[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-ci',

    [string]$ReportPath = '',

    [switch]$CacheOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

function Get-ReleaseX64ProjectText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    try {
        [xml]$project = Get-Content -LiteralPath $ProjectPath -Raw
    } catch {
        throw "Malformed generated Visual Studio project '$ProjectPath': $($_.Exception.Message)"
    }

    $matchingNodes = @(
        $project.SelectNodes('//*') |
            Where-Object {
                $condition = $_.GetAttribute('Condition')
                -not [string]::IsNullOrWhiteSpace($condition) -and
                    $condition -match "Release\|x64"
            }
    )
    if ($matchingNodes.Count -eq 0) {
        throw "No Release|x64 configuration found in $ProjectPath"
    }

    return ($matchingNodes | ForEach-Object { $_.OuterXml }) -join "`n"
}

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $BuildDirectory
$cachePath = Join-Path $buildDir 'CMakeCache.txt'
Assert-NoLtcgReleaseTree -CachePath $cachePath

$generatorPlatform = Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_GENERATOR_PLATFORM'
if ($generatorPlatform -ne 'x64') {
    throw "Expected CMAKE_GENERATOR_PLATFORM=x64, found '$generatorPlatform'."
}

$configurationTypes = Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_CONFIGURATION_TYPES'
if ($configurationTypes -ne 'Release') {
    throw "Expected exactly one generated configuration (Release), found '$configurationTypes'."
}

$report = [System.Collections.Generic.List[string]]::new()
$report.Add('DAWHERMES_ENABLE_LTCG=OFF')
$report.Add('CMAKE_GENERATOR_PLATFORM=x64')
$report.Add('CMAKE_CONFIGURATION_TYPES=Release')

if (-not $CacheOnly) {
    $targets = @(
        'dawhermes_core',
        'dawhermes_plugins',
        'dawhermes_audio',
        'dawhermes_midi',
        'dawhermes_hermes',
        'dawhermes_ui',
        'DAWHermes'
    )

    foreach ($target in $targets) {
        $projectPath = Join-Path $buildDir "$target.vcxproj"
        if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
            throw "Generated project not found for final target '$target': $projectPath"
        }

        $releaseText = Get-ReleaseX64ProjectText -ProjectPath $projectPath
        $state = Get-MsvcFlagState -Text $releaseText
        if ($state.HasActiveGl) {
            throw "Active /GL is forbidden for Release target '$target'."
        }
        if (-not $state.HasDisabledGl) {
            throw "Explicit /GL- was not found for Release target '$target'."
        }
        if (-not $state.HasReleaseOptimization) {
            throw "Normal Release optimization (/O2, /Ox or MaxSpeed) was not found for '$target'."
        }
        if ($releaseText -match '(?is)<WholeProgramOptimization>\s*true\s*</WholeProgramOptimization>') {
            throw "WholeProgramOptimization=true is forbidden for Release target '$target'."
        }

        $report.Add("${target}: compile=/GL- optimized")
    }

    $applicationProject = Join-Path $buildDir 'DAWHermes.vcxproj'
    $applicationReleaseText = Get-ReleaseX64ProjectText -ProjectPath $applicationProject
    $linkState = Get-MsvcFlagState -Text $applicationReleaseText
    if ($linkState.HasActiveLtcg) {
        throw 'Active /LTCG is forbidden for the DAWHermes Release linker.'
    }
    if (-not $linkState.HasDisabledLtcg) {
        throw 'Explicit /LTCG:OFF was not found for the DAWHermes Release linker.'
    }
    $report.Add('DAWHermes: linker=/LTCG:OFF')
}

if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $resolvedReportPath = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $ReportPath
    $reportDirectory = Split-Path -Parent $resolvedReportPath
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
    $report | Set-Content -LiteralPath $resolvedReportPath -Encoding UTF8
}

$report | ForEach-Object { Write-Host $_ }
if ($CacheOnly) {
    Write-Host 'Release x64 cache verification passed.'
} else {
    Write-Host 'Release compiler and linker flag verification passed.'
}
