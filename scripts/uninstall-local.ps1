Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

param(
    [switch]$RemoveUserData
)

$appRoot = Join-Path $env:LOCALAPPDATA 'DAWHermes'
$appInstallDir = Join-Path $appRoot 'app'
$startMenuShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\DAWHermes.lnk'
$desktopShortcut = Join-Path $env:USERPROFILE 'Desktop\DAWHermes.lnk'
$settingsRoot = Join-Path $env:APPDATA 'DAWHermes'

if (Test-Path $appInstallDir) {
    Remove-Item -Path $appInstallDir -Recurse -Force
}

if (Test-Path $startMenuShortcut) {
    Remove-Item -Path $startMenuShortcut -Force
}

if (Test-Path $desktopShortcut) {
    Remove-Item -Path $desktopShortcut -Force
}

if ($RemoveUserData) {
    if (Test-Path $appRoot) {
        Remove-Item -Path $appRoot -Recurse -Force
    }

    if (Test-Path $settingsRoot) {
        Remove-Item -Path $settingsRoot -Recurse -Force
    }

    Write-Host 'Removed DAWHermes application, shortcuts, logs, and settings.'
} else {
    Write-Host 'Removed DAWHermes application and shortcuts. Logs/settings were preserved.'
}
