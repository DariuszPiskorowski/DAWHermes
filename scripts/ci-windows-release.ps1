[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-ci',

    [string]$ArtifactDirectory = 'ci-artifacts\DAWHermes-Windows-x64-Release',

    [string]$LogDirectory = 'ci-logs',

    [ValidateRange(1, 4)]
    [int]$ParallelJobs = 1,

    [ValidateSet(
        'All',
        'ShowToolchain',
        'Configure',
        'VerifyCache',
        'BuildTests',
        'RunTests',
        'BuildApplication',
        'VerifyFlags',
        'VerifyX64',
        'Package',
        'VerifyPackage',
        'Summary')]
    [string]$Phase = 'All',

    [switch]$ResetDirectories
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $BuildDirectory
$artifactDir = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $ArtifactDirectory
$logDir = Resolve-RepositoryPath -RepoRoot $repoRoot -Path $LogDirectory
[void](Assert-PathWithinRoot -Path $buildDir -Root $repoRoot)
[void](Assert-PathWithinRoot -Path $artifactDir -Root $repoRoot)
[void](Assert-PathWithinRoot -Path $logDir -Root $repoRoot)

$resolvedPaths = @($buildDir, $artifactDir, $logDir)
if (@($resolvedPaths | Select-Object -Unique).Count -ne 3) {
    throw 'Build, artifact and log directories must be distinct.'
}
for ($leftIndex = 0; $leftIndex -lt $resolvedPaths.Count; $leftIndex++) {
    for ($rightIndex = 0; $rightIndex -lt $resolvedPaths.Count; $rightIndex++) {
        if ($leftIndex -eq $rightIndex) {
            continue
        }
        $left = $resolvedPaths[$leftIndex].TrimEnd('\', '/')
        $rightPrefix = $resolvedPaths[$rightIndex].TrimEnd('\', '/') +
            [System.IO.Path]::DirectorySeparatorChar
        if ($left.StartsWith($rightPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw 'Build, artifact and log directories must not contain one another.'
        }
    }
}

function Reset-CiDirectories {
    foreach ($directory in @($buildDir, (Split-Path -Parent $artifactDir), $logDir)) {
        [void](Assert-PathWithinRoot -Path $directory -Root $repoRoot)
        if (Test-Path -LiteralPath $directory) {
            Remove-Item -LiteralPath $directory -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path -Parent $artifactDir) -Force | Out-Null
}

function Write-SuccessMarker {
    param([Parameter(Mandatory = $true)][string]$Name)
    Set-Content -LiteralPath (Join-Path $logDir "$Name.success") `
        -Value ([DateTime]::UtcNow.ToString('o')) `
        -Encoding UTF8
}

function Invoke-LoggedPhase {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $transcriptPath = Join-Path $logDir ("{0}.log" -f $Name.ToLowerInvariant())
    $started = $false
    try {
        Start-Transcript -LiteralPath $transcriptPath -Force | Out-Null
        $started = $true
        Write-Host "===== $Name ====="
        & $Action
        Write-Host "===== ${Name}: PASSED ====="
    } catch {
        Write-Error "Phase '$Name' failed: $($_.Exception.Message)"
        throw
    } finally {
        if ($started) {
            Stop-Transcript | Out-Null
        }
    }
}

function Invoke-ShowToolchain {
    Invoke-LoggedPhase -Name 'Show toolchain' -Action {
        if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
            throw "Expected AMD64 runner architecture, found '$($env:PROCESSOR_ARCHITECTURE)'."
        }

        $pythonDetails = & python -c "import platform,struct,sys; print(platform.python_version()); print(struct.calcsize('P') * 8); print(sys.executable)"
        if ($LASTEXITCODE -ne 0 -or $pythonDetails.Count -lt 3) {
            throw 'Unable to query Python toolchain details.'
        }
        $pythonVersion = $pythonDetails[0].Trim()
        $pythonBits = $pythonDetails[1].Trim()
        if ($pythonVersion -notmatch '^3\.11\.' -or $pythonBits -ne '64') {
            throw "Expected Python 3.11 x64, found $pythonVersion ($pythonBits-bit)."
        }

        $cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')
        $msvc = Get-MsvcInfo
        $toolsVersionPath = Join-Path $msvc.InstallationPath 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
        if (-not (Test-Path -LiteralPath $toolsVersionPath -PathType Leaf)) {
            throw "MSVC tools version file not found: $toolsVersionPath"
        }
        $toolsVersion = (Get-Content -LiteralPath $toolsVersionPath -Raw).Trim()
        $compilerPath = Join-Path $msvc.InstallationPath "VC\Tools\MSVC\$toolsVersion\bin\Hostx64\x64\cl.exe"
        if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) {
            throw "x64 MSVC compiler not found: $compilerPath"
        }
        $compilerVersion = (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion
        $commit = (& git -C $repoRoot rev-parse HEAD).Trim()

        Write-Host "Python: $pythonVersion ($pythonBits-bit)"
        Write-Host "CMake: $($cmake.Version)"
        Write-Host "Visual Studio: $($msvc.InstallationVersion)"
        Write-Host "MSVC compiler: $compilerVersion"
        Write-Host "Runner architecture: $($env:PROCESSOR_ARCHITECTURE)"
        Write-Host "Git commit: $commit"

        @(
            "PythonVersion=$pythonVersion"
            "CMakeVersion=$($cmake.Version)"
            "VisualStudioVersion=$($msvc.InstallationVersion)"
            "MsvcVersion=$compilerVersion"
            "Commit=$commit"
        ) | Set-Content -LiteralPath (Join-Path $logDir 'toolchain-info.txt') -Encoding UTF8
        Write-SuccessMarker -Name 'toolchain'
    }
}

function Invoke-Configure {
    Invoke-LoggedPhase -Name 'Configure Release x64' -Action {
        & (Join-Path $PSScriptRoot 'configure.ps1') `
            -BuildDirectory $buildDir `
            -EnableLtcg OFF `
            -Configuration Release
        Write-SuccessMarker -Name 'configure'
    }
}

function Invoke-VerifyCache {
    Invoke-LoggedPhase -Name 'Verify LTCG cache' -Action {
        & (Join-Path $PSScriptRoot 'verify-release-flags.ps1') `
            -BuildDirectory $buildDir `
            -ReportPath (Join-Path $logDir 'ltcg-cache-verification.txt') `
            -CacheOnly
        Write-SuccessMarker -Name 'cache'
    }
}

function Invoke-BuildTests {
    Invoke-LoggedPhase -Name 'Build Release tests' -Action {
        & (Join-Path $PSScriptRoot 'test.ps1') `
            -BuildDirectory $buildDir `
            -Configuration Release `
            -ParallelJobs $ParallelJobs `
            -Mode BuildOnly
        Write-SuccessMarker -Name 'test-build'
    }
}

function Invoke-RunTests {
    Invoke-LoggedPhase -Name 'Run deterministic tests' -Action {
        & (Join-Path $PSScriptRoot 'test.ps1') `
            -BuildDirectory $buildDir `
            -Configuration Release `
            -ParallelJobs $ParallelJobs `
            -Mode RunOnly
        Write-SuccessMarker -Name 'deterministic-tests'
        Set-Content -LiteralPath (Join-Path $logDir 'embedded-hermes-status.txt') `
            -Value 'skipped (environment-dependent by policy)' `
            -Encoding UTF8
    }
}

function Invoke-BuildApplication {
    Invoke-LoggedPhase -Name 'Build Release application' -Action {
        & (Join-Path $PSScriptRoot 'build-release.ps1') `
            -BuildDirectory $buildDir `
            -ParallelJobs $ParallelJobs
        Write-SuccessMarker -Name 'application-build'
    }
}

function Invoke-VerifyFlags {
    Invoke-LoggedPhase -Name 'Verify Release flags' -Action {
        & (Join-Path $PSScriptRoot 'verify-release-flags.ps1') `
            -BuildDirectory $buildDir `
            -ReportPath (Join-Path $logDir 'release-flag-verification.txt')
        Write-SuccessMarker -Name 'release-flags'
    }
}

function Invoke-VerifyX64 {
    Invoke-LoggedPhase -Name 'Verify x64 executable' -Action {
        $executable = Get-BuiltExecutablePath -BuildDir $buildDir -Configuration Release
        $machine = Assert-WindowsX64Pe -Path $executable
        @(
            "Executable: DAWHermes.exe"
            ('PE machine: 0x{0:X4} (AMD64/x64)' -f $machine)
        ) | Set-Content -LiteralPath (Join-Path $logDir 'pe-x64-verification.txt') -Encoding UTF8
        Write-Host ('PE machine: 0x{0:X4} (AMD64/x64)' -f $machine)
        Write-SuccessMarker -Name 'x64'
    }
}

function Get-ToolchainValue {
    param([Parameter(Mandatory = $true)][string]$Name)

    $line = Get-Content -LiteralPath (Join-Path $logDir 'toolchain-info.txt') |
        Where-Object { $_ -like "$Name=*" } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "Toolchain metadata '$Name' is missing."
    }
    return $line.Substring($Name.Length + 1)
}

function Invoke-Package {
    Invoke-LoggedPhase -Name 'Package artifact' -Action {
        $refName = if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_REF_NAME)) {
            $env:GITHUB_REF_NAME
        } else {
            (& git -C $repoRoot branch --show-current).Trim()
        }
        $runId = if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_RUN_ID)) {
            $env:GITHUB_RUN_ID
        } else {
            'local-or-not-available'
        }
        $commit = (& git -C $repoRoot rev-parse HEAD).Trim()

        & (Join-Path $PSScriptRoot 'package-release-artifact.ps1') `
            -BuildDirectory $buildDir `
            -OutputDirectory $artifactDir `
            -LogDirectory $logDir `
            -CommitSha $commit `
            -RefName $refName `
            -WorkflowRunId $runId `
            -RunnerLabel 'windows-2022' `
            -PythonVersion (Get-ToolchainValue -Name 'PythonVersion') `
            -CMakeVersion (Get-ToolchainValue -Name 'CMakeVersion') `
            -MsvcVersion (Get-ToolchainValue -Name 'MsvcVersion') `
            -DeterministicTestResult passed `
            -EmbeddedHermesIntegrationStatus skipped
        Write-SuccessMarker -Name 'package'
    }
}

function Invoke-VerifyPackage {
    Invoke-LoggedPhase -Name 'Verify package' -Action {
        foreach ($requiredPath in @(
            (Join-Path $artifactDir 'app'),
            (Join-Path $artifactDir 'app\DAWHermes.exe'),
            (Join-Path $artifactDir 'Install-DAWHermes.ps1'),
            (Join-Path $artifactDir 'BUILD-INFO.txt'),
            (Join-Path $artifactDir 'SHA256SUMS.txt')
        )) {
            if (-not (Test-Path -LiteralPath $requiredPath)) {
                throw "Required artifact path is missing: $requiredPath"
            }
        }

        Assert-RuntimeHashManifest -ArtifactRoot $artifactDir
        Assert-WindowsX64Pe -Path (Join-Path $artifactDir 'app\DAWHermes.exe') | Out-Null
        & (Join-Path $artifactDir 'Install-DAWHermes.ps1') `
            -SourceDirectory $artifactDir `
            -ValidateOnly
        Write-SuccessMarker -Name 'package-verification'
    }
}

function Invoke-Summary {
    $status = @{
        Tests = Test-Path -LiteralPath (Join-Path $logDir 'deterministic-tests.success')
        Build = Test-Path -LiteralPath (Join-Path $logDir 'application-build.success')
        Ltcg = Test-Path -LiteralPath (Join-Path $logDir 'release-flags.success')
        X64 = Test-Path -LiteralPath (Join-Path $logDir 'x64.success')
        Package = Test-Path -LiteralPath (Join-Path $logDir 'package-verification.success')
    }
    $commit = (& git -C $repoRoot rev-parse HEAD).Trim()
    $artifactName = "DAWHermes-Windows-x64-Release-$commit"
    $resultText = {
        param([bool]$Value)
        if ($Value) { return 'passed' }
        return 'failed or not reached'
    }
    $summary = @(
        '# Windows x64 Release'
        ''
        "- Commit: ``$commit``"
        '- Configuration/platform: Release / Windows x64'
        "- Deterministic tests: $(& $resultText $status.Tests)"
        '- Embedded Hermes integration: skipped (environment-dependent by policy)'
        "- Application build: $(& $resultText $status.Build)"
        "- LTCG disabled verification: $(& $resultText $status.Ltcg)"
        "- PE x64 verification: $(& $resultText $status.X64)"
        "- Package verification: $(& $resultText $status.Package)"
        "- Runtime artifact: ``$artifactName``"
        '- Hosted runner scope: no real audio hardware, commercial VST3 scan, or manual validation is performed.'
    )
    $summaryPath = Join-Path $logDir 'workflow-summary.md'
    $summary | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
        $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding UTF8
    }
    $summary | ForEach-Object { Write-Host $_ }
}

if ($Phase -eq 'All' -or $ResetDirectories) {
    Reset-CiDirectories
} else {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}

$phaseActions = [ordered]@{
    ShowToolchain = { Invoke-ShowToolchain }
    Configure = { Invoke-Configure }
    VerifyCache = { Invoke-VerifyCache }
    BuildTests = { Invoke-BuildTests }
    RunTests = { Invoke-RunTests }
    BuildApplication = { Invoke-BuildApplication }
    VerifyFlags = { Invoke-VerifyFlags }
    VerifyX64 = { Invoke-VerifyX64 }
    Package = { Invoke-Package }
    VerifyPackage = { Invoke-VerifyPackage }
    Summary = { Invoke-Summary }
}

if ($Phase -eq 'All') {
    foreach ($action in $phaseActions.Values) {
        & $action
    }
} else {
    & $phaseActions[$Phase]
}
