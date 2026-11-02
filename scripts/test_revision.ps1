[CmdletBinding()]
param(
    [ValidateSet('Quick', 'Full', 'Audit')]
    [string]$Mode = 'Quick',
    [string]$ResultsDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$StartedUtc = (Get-Date).ToUniversalTime()
$RunId = $StartedUtc.ToString('yyyyMMdd-HHmmss')
if ([string]::IsNullOrWhiteSpace($ResultsDir)) {
    $ResultsDir = Join-Path $RepoRoot ("build/revision-test-results/{0}-{1}" -f $RunId, $Mode.ToLowerInvariant())
}
New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$StepResults = [System.Collections.Generic.List[object]]::new()
$FailedStep = $null

function Invoke-RevisionStep {
    param([string]$Name, [string]$FilePath, [string[]]$Arguments)

    $logPath = Join-Path $ResultsDir ((($Name -replace '[^A-Za-z0-9_.-]', '_')) + '.log')
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $global:LASTEXITCODE = 0
    $exitCode = 0
    $previousErrorActionPreference = $ErrorActionPreference
    Push-Location $RepoRoot
    try {
        $ErrorActionPreference = 'Continue'
        & $FilePath @Arguments *> $logPath
        if ($null -ne $LASTEXITCODE) { $exitCode = [int]$LASTEXITCODE }
    }
    catch {
        ($_ | Out-String) | Add-Content -Path $logPath
        $exitCode = 1
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
        Pop-Location
        $timer.Stop()
    }

    $StepResults.Add([pscustomobject]@{
        name = $Name
        result = $(if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' })
        exit_code = $exitCode
        duration_ms = $timer.ElapsedMilliseconds
        log = $logPath
    })
    return ($exitCode -eq 0)
}

$BuildDir = Join-Path $RepoRoot ("build/revision-{0}" -f $Mode.ToLowerInvariant())
$Steps = @(
    @{ Name='release-tooling'; File='python'; Args=@('scripts/test_release_v2.py') },
    @{ Name='release-tooling-optimized'; File='python'; Args=@('-O','scripts/test_release_v2.py') },
    @{ Name='protocol-v2-kit'; File='python'; Args=@('scripts/verify_protocol_v2.py') },
    @{ Name='protocol-v1-historical-kit'; File='python'; Args=@('scripts/import_protocol_kit.py','--verify') },
    @{ Name='cmake-configure'; File='cmake'; Args=@('-S','.','-B',$BuildDir,'-DPICO_OT_BUILD_TESTS=ON','-DCMAKE_BUILD_TYPE=Release') },
    @{ Name='cmake-build'; File='cmake'; Args=@('--build',$BuildDir,'--config','Release') },
    @{ Name='ctest'; File='ctest'; Args=@('--test-dir',$BuildDir,'-C','Release','--output-on-failure') }
)

if ($Mode -ne 'Quick') {
    $CapiBuildDir = Join-Path $RepoRoot 'build/revision-full-capi'
    $Steps += @(
        @{ Name='capi-configure'; File='cmake'; Args=@('-S','integrations/monaka','-B',$CapiBuildDir,'-DCMAKE_BUILD_TYPE=Release') },
        @{ Name='capi-build'; File='cmake'; Args=@('--build',$CapiBuildDir,'--config','Release') },
        @{ Name='capi-ctest'; File='ctest'; Args=@('--test-dir',$CapiBuildDir,'-C','Release','--output-on-failure') }
    )
}

# Product Quick/Full: actual PICO adapter, receiver, replay and C API tests.
# Audit: frozen kit / release-tool regressions and legacy source-space policy.
if ($Mode -ne 'Audit') {
    $AuditOnly = @('release-tooling', 'release-tooling-optimized', 'protocol-v2-kit', 'protocol-v1-historical-kit')
    $Steps = @($Steps | Where-Object { $_.Name -notin $AuditOnly })
    $CTest = @($Steps | Where-Object { $_.Name -eq 'ctest' })[0]
    if ($null -eq $CTest) { throw 'Product CTest step is missing' }
    $CTest['Args'] = @($CTest['Args']) + @('-E', '^(monaka_protocol_v1_kit_integrity|monaka_protocol_v2_kit_integrity|pico_source_space)$')
}

foreach ($step in $Steps) {
    if (-not (Invoke-RevisionStep -Name $step.Name -FilePath $step.File -Arguments $step.Args)) {
        $FailedStep = $step.Name
        break
    }
}

$head = ''
try { $head = ((& git -C $RepoRoot rev-parse HEAD 2>$null) | Out-String).Trim() } catch { }
$result = if ($null -eq $FailedStep) { 'PASS' } else { 'FAIL' }
$summary = [ordered]@{
    repo = 'orzkwsk/PicoMotionTrackerBridge'
    head = $head
    mode = $Mode
    validation_lane = $(if ($Mode -eq 'Audit') { 'HISTORICAL_SOURCE_POLICY_AUDIT' } else { 'PRODUCT_SOFTWARE' })
    source_scope_checks = $(if ($Mode -eq 'Audit') { 'INCLUDED' } else { 'NOT_RUN_SEPARATE_AUDIT' })
    calibration_policy_migration = 'NOT_VERIFIED - see MonakaVR/MonakaBridge issue #17'
    result = $result
    failed_step = $FailedStep
    started_utc = $StartedUtc.ToString('o')
    ended_utc = (Get-Date).ToUniversalTime().ToString('o')
    hardware_validation = 'NOT RUN'
    pico_hardware = 'NOT RUN'
    release_packaging = 'NOT RUN - run scripts/release_v2.py separately for source-bound v2 evidence'
    steps = $StepResults
}
$summaryPath = Join-Path $ResultsDir 'summary.json'
$summary | ConvertTo-Json -Depth 6 | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host ("RESULT={0} repo=PicoMotionTrackerBridge mode={1} failed_step={2} summary={3}" -f $result, $Mode, $FailedStep, $summaryPath)
if ($result -eq 'FAIL') { exit 1 }
