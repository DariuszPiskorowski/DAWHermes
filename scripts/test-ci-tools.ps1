[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Throws {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$Message
    )
    $threw = $false
    try {
        & $Action
    } catch {
        $threw = $true
    }
    if (-not $threw) {
        throw $Message
    }
}

function New-SyntheticPe {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][uint16]$Machine
    )

    $bytes = [byte[]]::new(256)
    $bytes[0] = 0x4D
    $bytes[1] = 0x5A
    [BitConverter]::GetBytes([uint32]0x80).CopyTo($bytes, 0x3C)
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    $bytes[0x82] = 0x00
    $bytes[0x83] = 0x00
    [BitConverter]::GetBytes($Machine).CopyTo($bytes, 0x84)
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'dawhermes-ci-tools-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$testRoot = (Resolve-Path -LiteralPath $testRoot).Path
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\', '/')
[void](Assert-PathWithinRoot -Path $testRoot -Root $tempRoot)

try {
    $cachePath = Join-Path $testRoot 'CMakeCache.txt'
    Set-Content -LiteralPath $cachePath `
        -Value 'DAWHERMES_ENABLE_LTCG:BOOL=OFF' `
        -Encoding UTF8
    Assert-NoLtcgReleaseTree -CachePath $cachePath
    Set-Content -LiteralPath $cachePath `
        -Value 'DAWHERMES_ENABLE_LTCG:BOOL=ON' `
        -Encoding UTF8
    Assert-Throws -Action {
        Assert-NoLtcgReleaseTree -CachePath $cachePath
    } -Message 'The LTCG cache parser accepted ON.'

    $safeFlags = Get-MsvcFlagState -Text '/GL- /O2 /LTCG:OFF'
    Assert-Condition $safeFlags.HasDisabledGl 'The parser did not find /GL-.'
    Assert-Condition (-not $safeFlags.HasActiveGl) 'The parser confused /GL- with active /GL.'
    Assert-Condition $safeFlags.HasReleaseOptimization 'The parser did not find /O2.'
    Assert-Condition $safeFlags.HasDisabledLtcg 'The parser did not find /LTCG:OFF.'
    Assert-Condition (-not $safeFlags.HasActiveLtcg) 'The parser confused /LTCG:OFF with active /LTCG.'

    $activeFlags = Get-MsvcFlagState -Text '/GL /O2 /LTCG'
    Assert-Condition $activeFlags.HasActiveGl 'Active /GL was not detected.'
    Assert-Condition $activeFlags.HasActiveLtcg 'Active /LTCG was not detected.'

    $malformedFlags = Get-MsvcFlagState -Text '/GLish /O2oops /LTCG:OFFish'
    Assert-Condition (-not $malformedFlags.HasDisabledGl) 'Malformed /GL input was accepted.'
    Assert-Condition (-not $malformedFlags.HasReleaseOptimization) 'Malformed optimization input was accepted.'
    Assert-Condition (-not $malformedFlags.HasDisabledLtcg) 'Malformed /LTCG input was accepted.'

    $syntheticBuild = Join-Path $testRoot 'synthetic-build'
    New-Item -ItemType Directory -Path $syntheticBuild | Out-Null
    @(
        'DAWHERMES_ENABLE_LTCG:BOOL=OFF'
        'CMAKE_GENERATOR_PLATFORM:INTERNAL=x64'
        'CMAKE_CONFIGURATION_TYPES:STRING=Release'
    ) | Set-Content -LiteralPath (Join-Path $syntheticBuild 'CMakeCache.txt') -Encoding UTF8
    $libraryProject = @'
<Project>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <WholeProgramOptimization>false</WholeProgramOptimization>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <AdditionalOptions>/GL- %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
'@
    $applicationProject = @'
<Project>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <WholeProgramOptimization>false</WholeProgramOptimization>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <AdditionalOptions>/GL- %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <Link>
      <AdditionalOptions>/LTCG:OFF %(AdditionalOptions)</AdditionalOptions>
    </Link>
  </ItemDefinitionGroup>
</Project>
'@
    $syntheticTargets = @(
        'dawhermes_core',
        'dawhermes_plugins',
        'dawhermes_audio',
        'dawhermes_midi',
        'dawhermes_hermes',
        'dawhermes_ui'
    )
    foreach ($target in $syntheticTargets) {
        Set-Content -LiteralPath (Join-Path $syntheticBuild "$target.vcxproj") `
            -Value $libraryProject `
            -Encoding UTF8
    }
    $applicationProjectPath = Join-Path $syntheticBuild 'DAWHermes.vcxproj'
    Set-Content -LiteralPath $applicationProjectPath -Value $applicationProject -Encoding UTF8
    & (Join-Path $PSScriptRoot 'verify-release-flags.ps1') `
        -BuildDirectory $syntheticBuild `
        -ReportPath (Join-Path $testRoot 'synthetic-flags-report.txt')

    $unsafeCompileProject = $libraryProject.Replace('/GL-', '/GL')
    $coreProjectPath = Join-Path $syntheticBuild 'dawhermes_core.vcxproj'
    Set-Content -LiteralPath $coreProjectPath -Value $unsafeCompileProject -Encoding UTF8
    Assert-Throws -Action {
        & (Join-Path $PSScriptRoot 'verify-release-flags.ps1') -BuildDirectory $syntheticBuild
    } -Message 'The generated-project parser accepted active /GL.'
    Set-Content -LiteralPath $coreProjectPath -Value $libraryProject -Encoding UTF8

    $unsafeLinkProject = $applicationProject.Replace('/LTCG:OFF', '/LTCG')
    Set-Content -LiteralPath $applicationProjectPath -Value $unsafeLinkProject -Encoding UTF8
    Assert-Throws -Action {
        & (Join-Path $PSScriptRoot 'verify-release-flags.ps1') -BuildDirectory $syntheticBuild
    } -Message 'The generated-project parser accepted active /LTCG.'
    Set-Content -LiteralPath $applicationProjectPath -Value '<Project><broken>' -Encoding UTF8
    Assert-Throws -Action {
        & (Join-Path $PSScriptRoot 'verify-release-flags.ps1') -BuildDirectory $syntheticBuild
    } -Message 'The generated-project parser accepted malformed XML.'

    $x64Path = Join-Path $testRoot 'x64.exe'
    $x86Path = Join-Path $testRoot 'x86.exe'
    $armPath = Join-Path $testRoot 'arm64.exe'
    $malformedPePath = Join-Path $testRoot 'malformed.exe'
    New-SyntheticPe -Path $x64Path -Machine 0x8664
    New-SyntheticPe -Path $x86Path -Machine 0x014C
    New-SyntheticPe -Path $armPath -Machine 0xAA64
    [System.IO.File]::WriteAllBytes($malformedPePath, [byte[]](0x4D, 0x5A))
    Assert-WindowsX64Pe -Path $x64Path | Out-Null
    Assert-Throws -Action { Assert-WindowsX64Pe -Path $x86Path } `
        -Message 'The x64 checker accepted an x86 fixture.'
    Assert-Throws -Action { Assert-WindowsX64Pe -Path $armPath } `
        -Message 'The x64 checker accepted an ARM64 fixture.'
    Assert-Throws -Action { Assert-WindowsX64Pe -Path $malformedPePath } `
        -Message 'The x64 checker accepted a malformed PE fixture.'

    $fakeBuild = Join-Path $testRoot 'fake-build'
    $fakeRuntime = Join-Path $fakeBuild 'DAWHermes_artefacts\Release'
    New-Item -ItemType Directory -Path $fakeRuntime -Force | Out-Null
    Copy-Item -LiteralPath $x64Path -Destination (Join-Path $fakeRuntime 'DAWHermes.exe')
    Set-Content -LiteralPath (Join-Path $fakeRuntime 'DAWHermes.pdb') `
        -Value 'must be omitted' `
        -Encoding UTF8
    $packagedArtifact = Join-Path $testRoot 'packaged-artifact'
    & (Join-Path $PSScriptRoot 'package-release-artifact.ps1') `
        -BuildDirectory $fakeBuild `
        -OutputDirectory $packagedArtifact `
        -LogDirectory (Join-Path $testRoot 'package-logs') `
        -SafetyRoot $testRoot `
        -CommitSha '0123456789abcdef0123456789abcdef01234567' `
        -RefName 'wip/ci1-test' `
        -WorkflowRunId 'synthetic' `
        -RunnerLabel 'synthetic-windows-x64' `
        -PythonVersion '3.11.0' `
        -CMakeVersion '3.30.0' `
        -MsvcVersion '19.40.00000' `
        -DeterministicTestResult passed `
        -EmbeddedHermesIntegrationStatus skipped
    Assert-RuntimeHashManifest -ArtifactRoot $packagedArtifact
    Assert-Condition `
        (-not (Test-Path -LiteralPath (Join-Path $packagedArtifact 'app\DAWHermes.pdb'))) `
        'The normal runtime artifact included a PDB.'
    & (Join-Path $packagedArtifact 'Install-DAWHermes.ps1') `
        -SourceDirectory $packagedArtifact `
        -ValidateOnly

    $artifactRoot = Join-Path $testRoot 'artifact'
    $appRoot = Join-Path $artifactRoot 'app'
    New-Item -ItemType Directory -Path $appRoot -Force | Out-Null
    Copy-Item -LiteralPath $x64Path -Destination (Join-Path $appRoot 'DAWHermes.exe')
    Set-Content -LiteralPath (Join-Path $appRoot 'runtime.dat') -Value 'runtime' -Encoding UTF8
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install-release-artifact.ps1') `
        -Destination (Join-Path $artifactRoot 'Install-DAWHermes.ps1')
    @(
        'Repository: DAWHermes'
        'Commit: 0123456789abcdef0123456789abcdef01234567'
        'Configuration: Release'
        'Platform: Windows x64'
        'DAWHERMES_ENABLE_LTCG: OFF'
        'Deterministic tests: passed'
        'Embedded Hermes integration: skipped'
    ) | Set-Content -LiteralPath (Join-Path $artifactRoot 'BUILD-INFO.txt') -Encoding UTF8

    $manifestPath = Join-Path $artifactRoot 'SHA256SUMS.txt'
    New-RuntimeHashManifest -ArtifactRoot $artifactRoot -ManifestPath $manifestPath
    Assert-RuntimeHashManifest -ArtifactRoot $artifactRoot
    & (Join-Path $artifactRoot 'Install-DAWHermes.ps1') `
        -SourceDirectory $artifactRoot `
        -ValidateOnly

    Add-Content -LiteralPath (Join-Path $appRoot 'runtime.dat') -Value 'corrupt' -Encoding UTF8
    Assert-Throws -Action {
        Assert-RuntimeHashManifest -ArtifactRoot $artifactRoot
    } -Message 'Hash verification accepted a modified runtime file.'

    Set-Content -LiteralPath (Join-Path $appRoot 'runtime.dat') -Value 'runtime' -Encoding UTF8
    New-RuntimeHashManifest -ArtifactRoot $artifactRoot -ManifestPath $manifestPath
    $firstHash = (Get-Content -LiteralPath $manifestPath | Select-Object -First 1).Substring(0, 64)
    Set-Content -LiteralPath $manifestPath `
        -Value "$firstHash  app/../escape.exe" `
        -Encoding UTF8
    Assert-Throws -Action {
        Assert-RuntimeHashManifest -ArtifactRoot $artifactRoot
    } -Message 'Manifest path traversal was accepted.'

    $invalidSource = Join-Path $testRoot 'invalid-artifact'
    New-Item -ItemType Directory -Path $invalidSource | Out-Null
    Assert-Throws -Action {
        & (Join-Path $PSScriptRoot 'install-release-artifact.ps1') `
            -SourceDirectory $invalidSource `
            -ValidateOnly
    } -Message 'Installer source-root validation accepted an incomplete artifact.'

    Write-Host 'CI helper-script tests passed.'
} finally {
    [void](Assert-PathWithinRoot -Path $testRoot -Root $tempRoot)
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
