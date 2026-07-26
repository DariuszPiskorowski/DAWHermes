Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RepoRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptRoot
    )

    return (Resolve-Path (Join-Path $ScriptRoot '..')).Path
}

function Get-VsWherePath {
    $path = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $path) {
        return $path
    }

    throw "vswhere.exe not found. Install Visual Studio Build Tools with Desktop development with C++ workload. Example: winget install Microsoft.VisualStudio.2022.BuildTools --override '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'"
}

function Get-MsvcInfo {
    $vswhere = Get-VsWherePath
    $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $installationVersion = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion

    if ([string]::IsNullOrWhiteSpace($installationPath) -or [string]::IsNullOrWhiteSpace($installationVersion)) {
        throw "MSVC toolchain not found. Install Visual Studio Build Tools with Desktop development with C++ workload (Microsoft.VisualStudio.Workload.VCTools)."
    }

    $majorVersion = [int]($installationVersion.Split('.')[0])
    $generator = if ($majorVersion -ge 17) { 'Visual Studio 17 2022' } else { 'Visual Studio 16 2019' }

    [pscustomobject]@{
        InstallationPath = $installationPath
        InstallationVersion = $installationVersion
        Generator = $generator
    }
}

function Get-CMakeVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CMakePath
    )

    if (-not (Test-Path $CMakePath)) {
        return $null
    }

    $firstLine = & $CMakePath --version | Select-Object -First 1
    if ($firstLine -match 'cmake version ([0-9]+\.[0-9]+\.[0-9]+)') {
        return [version]$Matches[1]
    }

    return $null
}

function Get-CMakePath {
    param(
        [version]$MinimumVersion = [version]'3.22.0'
    )

    $candidates = @()

    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        $candidates += $cmakeCommand.Source
    }

    $candidates += 'C:\Program Files\CMake\bin\cmake.exe'
    $candidates += 'C:\Program Files (x86)\CMake\bin\cmake.exe'
    $candidates += (Join-Path $env:LOCALAPPDATA 'Programs\CMake\bin\cmake.exe')

    $msvc = $null
    try {
        $msvc = Get-MsvcInfo
    } catch {
        # Do not block CMake discovery on this path.
    }

    if ($msvc) {
        $candidates += (Join-Path $msvc.InstallationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not $candidate) {
            continue
        }

        $version = Get-CMakeVersion -CMakePath $candidate
        if (-not $version) {
            continue
        }

        if ($version -ge $MinimumVersion) {
            return [pscustomobject]@{
                Path = $candidate
                Version = $version
            }
        }
    }

    throw "CMake $MinimumVersion or newer is required. Install it with: winget install Kitware.CMake --scope user"
}

function Get-BuildDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    return (Join-Path $RepoRoot 'build')
}

function Get-BuiltExecutablePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir,

        [ValidateSet('Debug', 'Release')]
        [string]$Configuration = 'Release'
    )

    $candidates = @(
        (Join-Path $BuildDir "DAWHermes_artefacts\$Configuration\DAWHermes.exe"),
        (Join-Path $BuildDir "$Configuration\DAWHermes.exe"),
        (Join-Path $BuildDir 'DAWHermes.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $discovered = Get-ChildItem -Path $BuildDir -Filter 'DAWHermes.exe' -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match [regex]::Escape($Configuration) -or $_.DirectoryName -match '_artefacts' } |
        Select-Object -First 1

    if ($discovered) {
        return $discovered.FullName
    }

    throw "DAWHermes.exe not found for configuration '$Configuration' under $BuildDir"
}
