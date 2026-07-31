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

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath)) {
        return ''
    }

    $escapedName = [regex]::Escape($Name)
    foreach ($line in Get-Content -LiteralPath $CachePath) {
        if ($line -match "^\s*$escapedName\s*(?::[^=]+)?\s*=\s*(.*?)\s*$") {
            return $Matches[1].Trim()
        }
    }

    return ''
}

function Assert-NoLtcgReleaseTree {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath
    )

    if (-not (Test-Path -LiteralPath $CachePath)) {
        throw "Release build tree is not configured: $CachePath"
    }

    $ltcgSetting = Get-CMakeCacheValue `
        -CachePath $CachePath `
        -Name 'DAWHERMES_ENABLE_LTCG'
    if ($ltcgSetting -ne 'OFF') {
        $message =
            "Unsafe Release build refused: DAWHERMES_ENABLE_LTCG must be exactly OFF " +
            "in $CachePath (found '$ltcgSetting'). Reconfigure with " +
            ".\scripts\configure.ps1 -EnableLtcg OFF."
        throw $message
    }
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

function Resolve-RepositoryPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'A non-empty path is required.'
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Assert-PathWithinRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Root,

        [switch]$AllowRoot
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')

    if ($fullPath.Equals($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        if ($AllowRoot) {
            return $fullPath
        }

        throw "Refusing to use the root directory itself: $fullPath"
    }

    $rootPrefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith(
            $rootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path '$fullPath' is outside the allowed root '$fullRoot'."
    }

    return $fullPath
}

function Get-PathRelativeToRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    [void](Assert-PathWithinRoot -Path $fullPath -Root $fullRoot)
    $rootPrefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    return $fullPath.Substring($rootPrefix.Length)
}

function Get-MsvcFlagState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $tokens = @(
        [regex]::Matches(
            $Text,
            '(?i)(?<![A-Za-z0-9])/(?:GL-?|O2|OX|LTCG(?::[A-Za-z]+)?)(?![A-Za-z0-9])') |
            ForEach-Object { $_.Value.ToUpperInvariant() }
    )

    [pscustomobject]@{
        HasDisabledGl = $tokens -contains '/GL-'
        HasActiveGl = $tokens -contains '/GL'
        HasReleaseOptimization =
            ($tokens -contains '/O2') -or
            ($tokens -contains '/OX') -or
            ($Text -match '(?is)<Optimization>\s*(MaxSpeed|Full)\s*</Optimization>')
        HasDisabledLtcg = $tokens -contains '/LTCG:OFF'
        HasActiveLtcg = @(
            $tokens | Where-Object {
                $_.StartsWith('/LTCG') -and $_ -ne '/LTCG:OFF'
            }
        ).Count -gt 0
    }
}

function Get-PeMachine {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "PE file not found: $Path"
    }

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 70) {
            throw "Malformed PE file (too short): $Path"
        }

        $reader = [System.IO.BinaryReader]::new($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5A4D) {
                throw "Malformed PE file (missing MZ signature): $Path"
            }

            $stream.Position = 0x3C
            $peOffset = $reader.ReadUInt32()
            if ($peOffset -gt ($stream.Length - 6)) {
                throw "Malformed PE file (invalid PE offset): $Path"
            }

            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) {
                throw "Malformed PE file (missing PE signature): $Path"
            }

            return $reader.ReadUInt16()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Assert-WindowsX64Pe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $machine = Get-PeMachine -Path $Path
    if ($machine -ne 0x8664) {
        throw ('Expected AMD64/x64 PE machine 0x8664, found 0x{0:X4}: {1}' -f
            $machine,
            $Path)
    }

    return $machine
}

function Test-SafeManifestRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [System.IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath.Contains(':')) {
        return $false
    }

    $normalized = $RelativePath.Replace('\', '/')
    if (-not $normalized.StartsWith('app/', [System.StringComparison]::Ordinal)) {
        return $false
    }

    $segments = $normalized.Split('/')
    return -not @($segments | Where-Object {
        [string]::IsNullOrWhiteSpace($_) -or $_ -eq '.' -or $_ -eq '..'
    }).Count
}

function New-RuntimeHashManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ArtifactRoot,

        [Parameter(Mandatory = $true)]
        [string]$ManifestPath
    )

    $root = [System.IO.Path]::GetFullPath($ArtifactRoot)
    $appDirectory = Join-Path $root 'app'
    if (-not (Test-Path -LiteralPath $appDirectory -PathType Container)) {
        throw "Artifact app directory not found: $appDirectory"
    }

    $files = @(Get-ChildItem -LiteralPath $appDirectory -Recurse -File)
    if ($files.Count -eq 0) {
        throw "Artifact app directory is empty: $appDirectory"
    }

    $lines = foreach ($file in $files) {
        $relative = (Get-PathRelativeToRoot -Path $file.FullName -Root $root).Replace('\', '/')
        if (-not (Test-SafeManifestRelativePath -RelativePath $relative)) {
            throw "Unsafe runtime manifest path: $relative"
        }

        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }

    @($lines | Sort-Object) |
        Set-Content -LiteralPath $ManifestPath -Encoding UTF8
}

function Assert-RuntimeHashManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ArtifactRoot,

        [string]$ManifestPath = ''
    )

    $root = [System.IO.Path]::GetFullPath($ArtifactRoot)
    if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
        $ManifestPath = Join-Path $root 'SHA256SUMS.txt'
    }

    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "SHA-256 manifest not found: $ManifestPath"
    }

    $entries = @{}
    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') {
            throw "Malformed SHA-256 manifest line: $line"
        }

        $expectedHash = $Matches[1].ToLowerInvariant()
        $relative = $Matches[2].Replace('\', '/')
        if (-not (Test-SafeManifestRelativePath -RelativePath $relative)) {
            throw "Unsafe SHA-256 manifest path: $relative"
        }
        if ($entries.ContainsKey($relative)) {
            throw "Duplicate SHA-256 manifest path: $relative"
        }

        $candidate = [System.IO.Path]::GetFullPath(
            (Join-Path $root ($relative.Replace('/', '\'))))
        [void](Assert-PathWithinRoot -Path $candidate -Root $root)
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Manifest runtime file not found: $relative"
        }

        $actualHash = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "SHA-256 mismatch for $relative"
        }

        $entries[$relative] = $true
    }

    if ($entries.Count -eq 0) {
        throw 'SHA-256 manifest contains no runtime files.'
    }

    $actualFiles = @(
        Get-ChildItem -LiteralPath (Join-Path $root 'app') -Recurse -File |
            ForEach-Object {
                (Get-PathRelativeToRoot -Path $_.FullName -Root $root).Replace('\', '/')
            }
    )
    foreach ($actualFile in $actualFiles) {
        if (-not $entries.ContainsKey($actualFile)) {
            throw "Runtime file is not covered by SHA256SUMS.txt: $actualFile"
        }
    }
    if ($actualFiles.Count -ne $entries.Count) {
        throw 'SHA-256 manifest entry count does not match the runtime payload.'
    }

    Write-Host "Verified $($entries.Count) runtime SHA-256 entries."
}
