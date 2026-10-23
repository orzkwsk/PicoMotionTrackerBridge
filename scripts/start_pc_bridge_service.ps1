param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

$buildScript = Join-Path $PSScriptRoot "build_pc_bridge_service.ps1"
$exe = (& $buildScript -Config $Config | Select-Object -Last 1)
if (-not $exe -or -not (Test-Path $exe)) {
    throw "PC Bridge Service executable was not resolved."
}

Write-Host "Starting PICO Receiver with its lifetime tray: $exe"
& (Join-Path $PSScriptRoot "steamvr_control_gui.ps1") -ServiceExecutable $exe -TrayOnly
if ($LASTEXITCODE -ne 0) { throw "PICO Receiver tray launch failed" }
