param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

$running = Get-Process -Name "pico_ot_bridge_service" -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "PC Bridge Service is already running (PID $($running[0].Id))."
    exit 0
}

$buildScript = Join-Path $PSScriptRoot "build_pc_bridge_service.ps1"
$exe = (& $buildScript -Config $Config | Select-Object -Last 1)
if (-not $exe -or -not (Test-Path $exe)) {
    throw "PC Bridge Service executable was not resolved."
}

Write-Host "Starting PC Bridge Service: $exe"
Start-Process -FilePath $exe -WorkingDirectory $RepoRoot -WindowStyle Hidden
Start-Sleep -Milliseconds 250

$running = Get-Process -Name "pico_ot_bridge_service" -ErrorAction SilentlyContinue
if (-not $running) {
    throw "PC Bridge Service exited immediately. Run $exe manually to inspect its error output."
}

Write-Host "PC Bridge Service started (PID $($running[0].Id))."
