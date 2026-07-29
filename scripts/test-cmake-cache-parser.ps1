[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
    'dawhermes-cmake-cache-parser-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$testDirectory = (Resolve-Path -LiteralPath $testDirectory).Path
$tempRoot = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath()).TrimEnd('\')
if (-not $testDirectory.StartsWith(
        $tempRoot + '\',
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing unsafe parser-test directory: $testDirectory"
}
$cachePath = Join-Path $testDirectory 'CMakeCache.txt'

try {
    $acceptedLines = @(
        'DAWHERMES_ENABLE_LTCG:BOOL=OFF',
        'DAWHERMES_ENABLE_LTCG:UNINITIALIZED=OFF',
        '  DAWHERMES_ENABLE_LTCG : BOOL =   OFF   '
    )

    foreach ($line in $acceptedLines) {
        Set-Content -LiteralPath $cachePath -Value $line -Encoding UTF8
        $value = Get-CMakeCacheValue `
            -CachePath $cachePath `
            -Name 'DAWHERMES_ENABLE_LTCG'
        if ($value -ne 'OFF') {
            throw "Expected OFF from cache line '$line', found '$value'."
        }
        Assert-NoLtcgReleaseTree -CachePath $cachePath
    }

    foreach ($rejectedContent in @(
        'DAWHERMES_ENABLE_LTCG:BOOL=ON',
        'SOME_OTHER_OPTION:BOOL=OFF'
    )) {
        Set-Content -LiteralPath $cachePath -Value $rejectedContent -Encoding UTF8
        $rejected = $false
        try {
            Assert-NoLtcgReleaseTree -CachePath $cachePath
        } catch {
            $rejected = $true
        }
        if (-not $rejected) {
            throw "Unsafe cache content was accepted: '$rejectedContent'."
        }
    }

    Write-Host 'CMake cache parser validation passed.'
} finally {
    Remove-Item -LiteralPath $testDirectory -Recurse -Force
}
