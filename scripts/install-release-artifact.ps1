[CmdletBinding()]
param(
    [string]$SourceDirectory = $PSScriptRoot,

    [switch]$ValidateOnly,

    [switch]$SkipShortcuts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ArtifactPeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "PE file not found: $Path"
    }
    $stream = [System.IO.File]::OpenRead($Path)
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

function Assert-ArtifactX64Pe {
    param([Parameter(Mandatory = $true)][string]$Path)

    $machine = Get-ArtifactPeMachine -Path $Path
    if ($machine -ne 0x8664) {
        throw ('Expected AMD64/x64 PE machine 0x8664, found 0x{0:X4}: {1}' -f
            $machine,
            $Path)
    }
}

function Test-ArtifactRelativePath {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

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

function Get-ArtifactRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $rootPrefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith(
            $rootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path '$fullPath' is outside artifact root '$fullRoot'."
    }
    return $fullPath.Substring($rootPrefix.Length)
}

function Assert-ArtifactBuildInfo {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "BUILD-INFO.txt not found: $Path"
    }
    $text = Get-Content -LiteralPath $Path -Raw
    $requiredPatterns = @(
        '(?m)^Repository: DAWHermes\r?$',
        '(?m)^Commit: [0-9a-fA-F]{40}\r?$',
        '(?m)^Configuration: Release\r?$',
        '(?m)^Platform: Windows x64\r?$',
        '(?m)^DAWHERMES_ENABLE_LTCG: OFF\r?$',
        '(?m)^Deterministic tests: passed\r?$',
        '(?m)^Embedded Hermes integration: (skipped|passed)\r?$'
    )
    foreach ($pattern in $requiredPatterns) {
        if ($text -notmatch $pattern) {
            throw "BUILD-INFO.txt is missing required metadata matching: $pattern"
        }
    }
}

function Assert-ArtifactHashes {
    param([Parameter(Mandatory = $true)][string]$Root)

    $manifestPath = Join-Path $Root 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "SHA256SUMS.txt not found: $manifestPath"
    }

    $entries = @{}
    foreach ($line in Get-Content -LiteralPath $manifestPath) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') {
            throw "Malformed SHA-256 manifest line: $line"
        }
        $expectedHash = $Matches[1].ToLowerInvariant()
        $relative = $Matches[2].Replace('\', '/')
        if (-not (Test-ArtifactRelativePath -RelativePath $relative)) {
            throw "Unsafe SHA-256 manifest path: $relative"
        }
        if ($entries.ContainsKey($relative)) {
            throw "Duplicate SHA-256 manifest path: $relative"
        }

        $candidate = [System.IO.Path]::GetFullPath(
            (Join-Path $Root ($relative.Replace('/', '\'))))
        $rootPrefix = $Root.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        if (-not $candidate.StartsWith(
                $rootPrefix,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Manifest path escapes the artifact root: $relative"
        }
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Manifest runtime file not found: $relative"
        }
        $actualHash = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "SHA-256 mismatch for $relative"
        }
        $entries[$relative] = $expectedHash
    }

    $actualFiles = @(
        Get-ChildItem -LiteralPath (Join-Path $Root 'app') -Recurse -File |
            ForEach-Object {
                (Get-ArtifactRelativePath -Path $_.FullName -Root $Root).Replace('\', '/')
            }
    )
    if ($entries.Count -eq 0 -or $actualFiles.Count -ne $entries.Count) {
        throw 'SHA-256 manifest does not cover the complete runtime payload.'
    }
    foreach ($actualFile in $actualFiles) {
        if (-not $entries.ContainsKey($actualFile)) {
            throw "Runtime file is not covered by SHA256SUMS.txt: $actualFile"
        }
    }

    return $entries
}

function New-OrUpdateArtifactShortcut {
    param(
        [Parameter(Mandatory = $true)][string]$ShortcutPath,
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    $shortcutDirectory = Split-Path -Parent $ShortcutPath
    New-Item -ItemType Directory -Path $shortcutDirectory -Force | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.WorkingDirectory = $WorkingDirectory
    $shortcut.IconLocation = "$TargetPath,0"
    $shortcut.Save()
}

$artifactRoot = [System.IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $artifactRoot -PathType Container)) {
    throw "Artifact directory not found: $artifactRoot"
}
if ($artifactRoot -eq [System.IO.Path]::GetPathRoot($artifactRoot).TrimEnd('\', '/')) {
    throw "Refusing to use a filesystem root as the artifact directory: $artifactRoot"
}

$appSource = Join-Path $artifactRoot 'app'
$sourceExecutable = Join-Path $appSource 'DAWHermes.exe'
if (-not (Test-Path -LiteralPath $appSource -PathType Container)) {
    throw "Artifact app directory not found: $appSource"
}
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Artifact executable not found: $sourceExecutable"
}

Assert-ArtifactBuildInfo -Path (Join-Path $artifactRoot 'BUILD-INFO.txt')
$hashEntries = Assert-ArtifactHashes -Root $artifactRoot
Assert-ArtifactX64Pe -Path $sourceExecutable
Write-Host "Artifact validation passed: $artifactRoot"

if ($ValidateOnly) {
    return
}

if (@(Get-Process -Name 'DAWHermes' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'DAWHermes is running. Close it before installing the artifact.'
}

$defaultInstallDirectory = Join-Path $env:LOCALAPPDATA 'DAWHermes\app'
$appInstallDirectory = [System.IO.Path]::GetFullPath($defaultInstallDirectory)
$installParent = Split-Path -Parent $appInstallDirectory
New-Item -ItemType Directory -Path $installParent -Force | Out-Null

$stagingDirectory = Join-Path $installParent ('app.installing-' + [guid]::NewGuid().ToString('N'))
$previousDirectory = Join-Path $installParent ('app.previous-' + [guid]::NewGuid().ToString('N'))
$installed = $false
try {
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    Copy-Item -Path (Join-Path $appSource '*') -Destination $stagingDirectory -Recurse -Force

    foreach ($entry in $hashEntries.GetEnumerator()) {
        $relativeWithinApp = $entry.Key.Substring(4).Replace('/', '\')
        $stagedFile = Join-Path $stagingDirectory $relativeWithinApp
        $stagedHash = (Get-FileHash -LiteralPath $stagedFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($stagedHash -ne $entry.Value) {
            throw "Installed staging hash mismatch for $($entry.Key)"
        }
    }
    Assert-ArtifactX64Pe -Path (Join-Path $stagingDirectory 'DAWHermes.exe')

    if (Test-Path -LiteralPath $appInstallDirectory) {
        Move-Item -LiteralPath $appInstallDirectory -Destination $previousDirectory
    }
    Move-Item -LiteralPath $stagingDirectory -Destination $appInstallDirectory
    $installed = $true
    if (Test-Path -LiteralPath $previousDirectory) {
        Remove-Item -LiteralPath $previousDirectory -Recurse -Force
    }
} catch {
    if (-not $installed -and
        -not (Test-Path -LiteralPath $appInstallDirectory) -and
        (Test-Path -LiteralPath $previousDirectory)) {
        Move-Item -LiteralPath $previousDirectory -Destination $appInstallDirectory
    }
    throw
} finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
}

$installedExecutable = Join-Path $appInstallDirectory 'DAWHermes.exe'
if (-not $SkipShortcuts) {
    $startMenuShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\DAWHermes.lnk'
    $desktopShortcut = Join-Path $env:USERPROFILE 'Desktop\DAWHermes.lnk'
    New-OrUpdateArtifactShortcut `
        -ShortcutPath $startMenuShortcut `
        -TargetPath $installedExecutable `
        -WorkingDirectory $appInstallDirectory
    New-OrUpdateArtifactShortcut `
        -ShortcutPath $desktopShortcut `
        -TargetPath $installedExecutable `
        -WorkingDirectory $appInstallDirectory
    Write-Host "Start Menu shortcut: $startMenuShortcut"
    Write-Host "Desktop shortcut: $desktopShortcut"
}

Write-Host "Installed executable: $installedExecutable"
Write-Host 'Installation used the precompiled artifact; no configure, compile or link step ran.'
