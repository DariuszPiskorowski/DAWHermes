[CmdletBinding()]
param(
    [switch]$Diagnostic,

    [ValidateRange(0, 256)]
    [int]$ParallelJobs = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

function Format-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [Parameter(Mandatory = $true)]
        [string[]]$ArgumentList
    )

    $formattedArguments = foreach ($argument in $ArgumentList) {
        if ($argument -match '[\s"]') {
            '"' + ($argument -replace '"', '\"') + '"'
        } else {
            $argument
        }
    }

    return "$Executable $($formattedArguments -join ' ')"
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

    $match = Get-Content -LiteralPath $CachePath |
        Where-Object { $_ -match "^$([regex]::Escape($Name))(:[^=]+)?=(.*)$" } |
        Select-Object -First 1

    if ($match -and $match -match '^[^=]+=(.*)$') {
        return $Matches[1]
    }

    return ''
}

function Write-DiagnosticLine {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    $line = '{0:o} {1}' -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $script:phaseLogPath -Value $line -Encoding UTF8
}

function Write-DiagnosticCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command
    )

    $line = '{0:o} {1}' -f (Get-Date), $Command
    Write-Host "COMMAND: $Command"
    Add-Content -LiteralPath $script:commandLogPath -Value $line -Encoding UTF8
}

function Write-SystemSnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not $Diagnostic) {
        return
    }

    Write-DiagnosticLine "SNAPSHOT START: $Label"

    try {
        $operatingSystem = Get-CimInstance Win32_OperatingSystem
        $computerSystem = Get-CimInstance Win32_ComputerSystem
        $pageFiles = @(Get-CimInstance Win32_PageFileUsage -ErrorAction SilentlyContinue)
        $diskRoot = [System.IO.Path]::GetPathRoot($buildDir)
        $driveName = $diskRoot.TrimEnd('\').TrimEnd(':')
        $drive = Get-PSDrive -Name $driveName

        $memorySummary = [pscustomobject]@{
            TotalPhysicalMB = [math]::Round(
                $computerSystem.TotalPhysicalMemory / 1MB,
                1)
            AvailablePhysicalMB = [math]::Round(
                $operatingSystem.FreePhysicalMemory / 1KB,
                1)
            TotalVirtualMB = [math]::Round(
                $operatingSystem.TotalVirtualMemorySize / 1KB,
                1)
            AvailableVirtualMB = [math]::Round(
                $operatingSystem.FreeVirtualMemory / 1KB,
                1)
            PageFileAllocatedMB = [math]::Round(
                ($pageFiles | Measure-Object AllocatedBaseSize -Sum).Sum,
                1)
            PageFileCurrentUsageMB = [math]::Round(
                ($pageFiles | Measure-Object CurrentUsage -Sum).Sum,
                1)
            PageFilePeakUsageMB = [math]::Round(
                ($pageFiles | Measure-Object PeakUsage -Sum).Sum,
                1)
            BuildDriveFreeGB = [math]::Round($drive.Free / 1GB, 2)
            BuildDriveUsedGB = [math]::Round($drive.Used / 1GB, 2)
        }

        $memorySummary |
            Format-List |
            Out-String |
            ForEach-Object {
                Add-Content -LiteralPath $script:snapshotLogPath -Value $_ -Encoding UTF8
                Write-Host $_
            }

        $buildProcesses = @(
            Get-Process -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.ProcessName -in @(
                        'cmake',
                        'MSBuild',
                        'cl',
                        'link',
                        'mspdbsrv',
                        'c2',
                        'rc')
                } |
                Sort-Object ProcessName, Id |
                Select-Object ProcessName, Id, CPU,
                    @{ Name = 'WorkingSetMB'; Expression = {
                        [math]::Round($_.WorkingSet64 / 1MB, 1)
                    } },
                    @{ Name = 'PrivateMemoryMB'; Expression = {
                        [math]::Round($_.PrivateMemorySize64 / 1MB, 1)
                    } }
        )

        if ($buildProcesses.Count -eq 0) {
            Add-Content -LiteralPath $script:snapshotLogPath `
                -Value 'Active build processes: none' `
                -Encoding UTF8
            Write-Host 'Active build processes: none'
        } else {
            $buildProcesses |
                Format-Table -AutoSize |
                Out-String |
                ForEach-Object {
                    Add-Content -LiteralPath $script:snapshotLogPath -Value $_ -Encoding UTF8
                    Write-Host $_
                }
        }

        Get-Process -ErrorAction SilentlyContinue |
            Sort-Object WorkingSet64 -Descending |
            Select-Object -First 12 ProcessName, Id,
                @{ Name = 'WorkingSetMB'; Expression = {
                    [math]::Round($_.WorkingSet64 / 1MB, 1)
                } },
                @{ Name = 'PrivateMemoryMB'; Expression = {
                    [math]::Round($_.PrivateMemorySize64 / 1MB, 1)
                } } |
            Format-Table -AutoSize |
            Out-String |
            ForEach-Object {
                Add-Content -LiteralPath $script:snapshotLogPath -Value $_ -Encoding UTF8
                Write-Host $_
            }
    } catch {
        Write-DiagnosticLine "SNAPSHOT ERROR: $Label - $($_.Exception.Message)"
    }

    Write-DiagnosticLine "SNAPSHOT END: $Label"
}

function Write-BuildArtifactSnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not $Diagnostic) {
        return
    }

    Write-DiagnosticLine "ARTIFACT SNAPSHOT START: $Label"

    $artifactPaths = @(
        (Join-Path $buildDir 'Release\dawhermes_core.lib'),
        (Join-Path $buildDir 'Release\dawhermes_audio.lib'),
        (Join-Path $buildDir 'Release\dawhermes_hermes.lib'),
        (Join-Path $buildDir 'Release\dawhermes_midi.lib'),
        (Join-Path $buildDir 'Release\dawhermes_ui.lib'),
        (Join-Path $buildDir 'DAWHermes_artefacts\Release\DAWHermes.exe'),
        (Join-Path $buildDir 'DAWHermes_artefacts\Release\DAWHermes.pdb'),
        (Join-Path $buildDir 'DAWHermes_artefacts\Release\DAWHermes.ilk'),
        (Join-Path $buildDir 'DAWHermes_artefacts\Release\DAWHermes.lib'),
        (Join-Path $buildDir 'DAWHermes.dir\Release\DAWHermes.tlog\DAWHermes.lastbuildstate'),
        (Join-Path $buildDir 'DAWHermes.dir\Release\DAWHermes.tlog\unsuccessfulbuild'),
        (Join-Path $buildDir 'DAWHermes.dir\Release\DAWHermes.tlog\link.command.1.tlog'),
        (Join-Path $buildDir 'DAWHermes.dir\Release\DAWHermes.tlog\link.read.1.tlog'),
        (Join-Path $buildDir 'DAWHermes.dir\Release\DAWHermes.tlog\link.write.1.tlog')
    )

    foreach ($path in $artifactPaths) {
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            $line = '{0:o}`t{1}`t{2}' -f $item.LastWriteTime, $item.Length, $item.FullName
        } else {
            $line = "MISSING`t$path"
        }

        Add-Content -LiteralPath $script:artifactLogPath -Value $line -Encoding UTF8
        Write-Host $line
    }

    Write-DiagnosticLine "ARTIFACT SNAPSHOT END: $Label"
}

function Start-ResourceMonitor {
    if (-not $Diagnostic) {
        return $null
    }

    $monitorPath = $script:resourceMonitorPath
    $stopPath = $script:resourceMonitorStopPath
    $driveName = [System.IO.Path]::GetPathRoot($buildDir).TrimEnd('\').TrimEnd(':')

    return Start-Job -ScriptBlock {
        param(
            [string]$OutputPath,
            [string]$StopPath,
            [string]$BuildDriveName
        )

        '"Timestamp","AvailablePhysicalMB","CommitUsedMB","PageFileUsageMB","BuildDriveFreeGB","BuildProcessCount","BuildWorkingSetMB","ClCount","LinkCount","MSBuildCount","MspdbsrvCount","Processes"' |
            Set-Content -LiteralPath $OutputPath -Encoding UTF8

        while (-not (Test-Path -LiteralPath $StopPath)) {
            try {
                $operatingSystem = Get-CimInstance Win32_OperatingSystem
                $pageFiles = @(Get-CimInstance Win32_PageFileUsage -ErrorAction SilentlyContinue)
                $drive = Get-PSDrive -Name $BuildDriveName
                $processes = @(
                    Get-Process -ErrorAction SilentlyContinue |
                        Where-Object {
                            $_.ProcessName -in @(
                                'cmake',
                                'MSBuild',
                                'cl',
                                'link',
                                'mspdbsrv',
                                'c2',
                                'rc')
                        }
                )

                $workingSetBytes = ($processes | Measure-Object WorkingSet64 -Sum).Sum
                if ($null -eq $workingSetBytes) {
                    $workingSetBytes = 0
                }

                $processText = (
                    $processes |
                        Sort-Object ProcessName, Id |
                        ForEach-Object {
                            '{0}:{1}:{2}MB' -f
                                $_.ProcessName,
                                $_.Id,
                                [math]::Round($_.WorkingSet64 / 1MB, 1)
                        }
                ) -join ';'

                $availablePhysicalMB = [math]::Round(
                    $operatingSystem.FreePhysicalMemory / 1KB,
                    1)
                $commitUsedMB = [math]::Round(
                    ($operatingSystem.TotalVirtualMemorySize - $operatingSystem.FreeVirtualMemory) / 1KB,
                    1)
                $pageFileUsageMB = [math]::Round(
                    ($pageFiles | Measure-Object CurrentUsage -Sum).Sum,
                    1)

                $line = '"{0}",{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},"{11}"' -f
                    (Get-Date).ToString('o'),
                    $availablePhysicalMB,
                    $commitUsedMB,
                    $pageFileUsageMB,
                    [math]::Round($drive.Free / 1GB, 2),
                    $processes.Count,
                    [math]::Round($workingSetBytes / 1MB, 1),
                    @($processes | Where-Object ProcessName -eq 'cl').Count,
                    @($processes | Where-Object ProcessName -eq 'link').Count,
                    @($processes | Where-Object ProcessName -eq 'MSBuild').Count,
                    @($processes | Where-Object ProcessName -eq 'mspdbsrv').Count,
                    ($processText -replace '"', '""')

                Add-Content -LiteralPath $OutputPath -Value $line -Encoding UTF8
            } catch {
                $line = '"{0}",,,,,,,,,,,"MONITOR ERROR: {1}"' -f
                    (Get-Date).ToString('o'),
                    ($_.Exception.Message -replace '"', '""')
                Add-Content -LiteralPath $OutputPath -Value $line -Encoding UTF8
            }

            Start-Sleep -Seconds 1
        }
    } -ArgumentList $monitorPath, $stopPath, $driveName
}

function Stop-ResourceMonitor {
    param(
        [System.Management.Automation.Job]$Job
    )

    if ($null -eq $Job) {
        return
    }

    New-Item -Path $script:resourceMonitorStopPath -ItemType File -Force | Out-Null
    Wait-Job -Job $Job -Timeout 10 | Out-Null
    if ($Job.State -eq 'Running') {
        Stop-Job -Job $Job
    }
    Receive-Job -Job $Job -ErrorAction SilentlyContinue | Out-Null
    Remove-Job -Job $Job -Force
}

function Write-ResourceSummary {
    if (-not $Diagnostic -or -not (Test-Path -LiteralPath $script:resourceMonitorPath)) {
        return
    }

    $samples = @(Import-Csv -LiteralPath $script:resourceMonitorPath)
    if ($samples.Count -eq 0) {
        return
    }

    $numericSamples = @(
        $samples |
            Where-Object {
                -not [string]::IsNullOrWhiteSpace($_.AvailablePhysicalMB)
            }
    )
    if ($numericSamples.Count -eq 0) {
        return
    }

    $summary = [pscustomobject]@{
        SampleCount = $numericSamples.Count
        MinimumAvailablePhysicalMB = [math]::Round(
            ($numericSamples.AvailablePhysicalMB |
                ForEach-Object { [double]$_ } |
                Measure-Object -Minimum).Minimum,
            1)
        PeakCommitUsedMB = [math]::Round(
            ($numericSamples.CommitUsedMB |
                ForEach-Object { [double]$_ } |
                Measure-Object -Maximum).Maximum,
            1)
        PeakPageFileUsageMB = [math]::Round(
            ($numericSamples.PageFileUsageMB |
                ForEach-Object { [double]$_ } |
                Measure-Object -Maximum).Maximum,
            1)
        PeakBuildWorkingSetMB = [math]::Round(
            ($numericSamples.BuildWorkingSetMB |
                ForEach-Object { [double]$_ } |
                Measure-Object -Maximum).Maximum,
            1)
        PeakBuildProcessCount = (
            $numericSamples.BuildProcessCount |
                ForEach-Object { [int]$_ } |
                Measure-Object -Maximum).Maximum
        PeakClCount = (
            $numericSamples.ClCount |
                ForEach-Object { [int]$_ } |
                Measure-Object -Maximum).Maximum
        PeakLinkCount = (
            $numericSamples.LinkCount |
                ForEach-Object { [int]$_ } |
                Measure-Object -Maximum).Maximum
        MinimumBuildDriveFreeGB = [math]::Round(
            ($numericSamples.BuildDriveFreeGB |
                ForEach-Object { [double]$_ } |
                Measure-Object -Minimum).Minimum,
            2)
    }

    $summary |
        Format-List |
        Out-String |
        Set-Content -LiteralPath $script:resourceSummaryPath -Encoding UTF8

    Write-Host ($summary | Format-List | Out-String)
}

$repoRoot = Get-RepoRoot -ScriptRoot $PSScriptRoot
$buildDir = Get-BuildDir -RepoRoot $repoRoot
$cmake = Get-CMakePath -MinimumVersion ([version]'3.22.0')

$effectiveParallelJobs = $ParallelJobs
if ($Diagnostic -and $effectiveParallelJobs -eq 0) {
    $effectiveParallelJobs = 1
}

$transcriptStarted = $false
$resourceMonitorJob = $null
$buildStopwatch = $null

if ($Diagnostic) {
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $diagnosticDir = Join-Path $buildDir "diagnostics\release-$timestamp"
    New-Item -Path $diagnosticDir -ItemType Directory -Force | Out-Null

    $script:phaseLogPath = Join-Path $diagnosticDir 'phase-markers.log'
    $script:commandLogPath = Join-Path $diagnosticDir 'commands.log'
    $script:snapshotLogPath = Join-Path $diagnosticDir 'system-snapshots.log'
    $script:artifactLogPath = Join-Path $diagnosticDir 'artifact-timeline.log'
    $script:resourceMonitorPath = Join-Path $diagnosticDir 'resource-samples.csv'
    $script:resourceMonitorStopPath = Join-Path $diagnosticDir 'resource-monitor.stop'
    $script:resourceSummaryPath = Join-Path $diagnosticDir 'resource-summary.txt'
    $binaryLogPath = Join-Path $diagnosticDir 'msbuild.binlog'
    $textLogPath = Join-Path $diagnosticDir 'msbuild-diagnostic.log'
    $transcriptPath = Join-Path $diagnosticDir 'powershell-transcript.log'

    Start-Transcript -LiteralPath $transcriptPath -Force | Out-Null
    $transcriptStarted = $true

    Write-DiagnosticLine 'SCRIPT START'
    Write-DiagnosticLine "Diagnostic directory: $diagnosticDir"
    Write-DiagnosticLine "Requested parallel jobs: $effectiveParallelJobs"
    Write-DiagnosticLine "CMake: $($cmake.Path) ($($cmake.Version))"

    $cachePath = Join-Path $buildDir 'CMakeCache.txt'
    Write-DiagnosticLine "Generator: $(Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_GENERATOR')"
    Write-DiagnosticLine "Generator instance: $(Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_GENERATOR_INSTANCE')"
    Write-DiagnosticLine "MSBuild: $(Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_VS_MSBUILD_COMMAND')"

    $msvc = Get-MsvcInfo
    Write-DiagnosticLine "MSVC installation: $($msvc.InstallationPath) ($($msvc.InstallationVersion))"
    Write-DiagnosticLine "Logical processors: $([Environment]::ProcessorCount)"
    Write-DiagnosticLine "CMAKE_BUILD_PARALLEL_LEVEL: $($env:CMAKE_BUILD_PARALLEL_LEVEL)"

    Write-SystemSnapshot -Label 'script-start'
    Write-BuildArtifactSnapshot -Label 'before-build'
    $resourceMonitorJob = Start-ResourceMonitor
}

try {
    if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        if ($Diagnostic) {
            Write-DiagnosticLine 'PHASE START: CONFIGURE'
            Write-SystemSnapshot -Label 'before-configure'
            Write-DiagnosticCommand (Join-Path $PSScriptRoot 'configure.ps1')
        }

        & (Join-Path $PSScriptRoot 'configure.ps1')

        if ($Diagnostic) {
            Write-SystemSnapshot -Label 'after-configure'
            Write-DiagnosticLine 'PHASE END: CONFIGURE'
        }
    } elseif ($Diagnostic) {
        Write-DiagnosticLine 'PHASE SKIPPED: CONFIGURE (existing CMakeCache.txt)'
    }

    $buildArguments = @(
        '--build',
        $buildDir,
        '--config',
        'Release',
        '--target',
        'DAWHermes'
    )

    if ($effectiveParallelJobs -gt 0) {
        $buildArguments += @('--parallel', $effectiveParallelJobs.ToString())
    }

    if ($Diagnostic) {
        $buildArguments += @(
            '--',
            "/p:CL_MPCount=$effectiveParallelJobs",
            "/bl:$binaryLogPath;ProjectImports=None",
            '/fl',
            "/flp:LogFile=$textLogPath;Verbosity=diagnostic",
            '/v:normal'
        )
    }

    if ($Diagnostic) {
        Write-DiagnosticLine 'PHASE START: RELEASE BUILD'
        Write-SystemSnapshot -Label 'before-release-build'
        Write-DiagnosticCommand (
            Format-LoggedCommand -Executable $cmake.Path -ArgumentList $buildArguments)
        $buildStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    }

    & $cmake.Path @buildArguments
    $buildExitCode = $LASTEXITCODE

    if ($Diagnostic) {
        $buildStopwatch.Stop()
        Write-DiagnosticLine "RELEASE BUILD EXIT CODE: $buildExitCode"
        Write-DiagnosticLine "RELEASE BUILD ELAPSED: $($buildStopwatch.Elapsed)"
    }

    if ($buildExitCode -ne 0) {
        throw "Release build failed with exit code $buildExitCode"
    }

    if ($Diagnostic) {
        Write-SystemSnapshot -Label 'after-release-build'
        Write-BuildArtifactSnapshot -Label 'after-build'
        Write-DiagnosticLine 'PHASE END: RELEASE BUILD'
    }

    $exePath = Get-BuiltExecutablePath -BuildDir $buildDir -Configuration Release
    Write-Host "Release executable: $exePath"

    if ($Diagnostic) {
        Write-DiagnosticLine "ARTIFACT CHECK PASSED: $exePath"
        Write-DiagnosticLine 'SCRIPT SUCCESS'
    }
} catch {
    if ($Diagnostic) {
        if ($null -ne $buildStopwatch -and $buildStopwatch.IsRunning) {
            $buildStopwatch.Stop()
        }
        Write-DiagnosticLine "SCRIPT FAILURE: $($_.Exception.Message)"
        Write-SystemSnapshot -Label 'failure'
        Write-BuildArtifactSnapshot -Label 'failure'
    }

    throw
} finally {
    if ($Diagnostic) {
        Stop-ResourceMonitor -Job $resourceMonitorJob
        Write-ResourceSummary
        Write-DiagnosticLine 'SCRIPT END'
        Write-Host "Diagnostic logs: $diagnosticDir"
    }

    if ($transcriptStarted) {
        Stop-Transcript | Out-Null
    }
}
