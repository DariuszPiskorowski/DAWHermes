[CmdletBinding()]
param(
    [switch]$SkipBuild,

    [string]$BuildDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

function New-OrUpdateShortcut {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ShortcutPath,

        [Parameter(Mandatory = $true)]
        [string]$TargetPath,

        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory
    )

    $shortcutDir = Split-Path -Parent $ShortcutPath
    if (-not (Test-Path $shortcutDir)) {
        New-Item -ItemType Directory -Path $shortcutDir -Force | Out-Null
    }

    $wshShell = New-Object -ComObject WScript.Shell
    $shortcut = $wshShell.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.WorkingDirectory = $WorkingDirectory
    $shortcut.IconLocation = "$TargetPath,0"
    $shortcut.Save()
}

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Get-BuildDir -RepoRoot $repoRoot
} elseif ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}

if ($SkipBuild) {
    $cachePath = Join-Path $buildDir 'CMakeCache.txt'
    $ltcgSetting = Get-Content -LiteralPath $cachePath |
        Where-Object { $_ -match '^DAWHERMES_ENABLE_LTCG:[^=]+=(.*)$' } |
        Select-Object -First 1
    if ($ltcgSetting -ne 'DAWHERMES_ENABLE_LTCG:BOOL=OFF') {
        $message =
            "Unsafe install refused: the existing Release artifact must come " +
            "from a build tree with DAWHERMES_ENABLE_LTCG=OFF."
        throw $message
    }
} else {
    & (Join-Path $PSScriptRoot 'build-release.ps1') `
        -BuildDirectory $BuildDirectory
}

$sourceExe = Get-BuiltExecutablePath -BuildDir $buildDir -Configuration Release
$sourceDir = Split-Path -Parent $sourceExe

$localAppDataRoot = Join-Path $env:LOCALAPPDATA 'DAWHermes'
$appInstallDir = Join-Path $localAppDataRoot 'app'

if (Test-Path $appInstallDir) {
    Remove-Item -Path $appInstallDir -Recurse -Force
}
New-Item -Path $appInstallDir -ItemType Directory -Force | Out-Null

Copy-Item -Path (Join-Path $sourceDir '*') -Destination $appInstallDir -Recurse -Force

$installedExe = Join-Path $appInstallDir 'DAWHermes.exe'
if (-not (Test-Path $installedExe)) {
    throw "Installed executable missing at $installedExe"
}

$startMenuShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\DAWHermes.lnk'
$desktopShortcut = Join-Path $env:USERPROFILE 'Desktop\DAWHermes.lnk'

New-OrUpdateShortcut -ShortcutPath $startMenuShortcut -TargetPath $installedExe -WorkingDirectory $appInstallDir
New-OrUpdateShortcut -ShortcutPath $desktopShortcut -TargetPath $installedExe -WorkingDirectory $appInstallDir

Write-Host "Installed executable: $installedExe"
Write-Host "Start Menu shortcut: $startMenuShortcut"
Write-Host "Desktop shortcut: $desktopShortcut"
