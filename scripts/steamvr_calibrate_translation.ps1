param(
    [ValidateSet("List", "Calibrate", "Clear")]
    [string]$Action = "List",

    [string]$TrackerSerial = "",

    [ValidateSet("Left", "Right")]
    [string]$Controller = "Right",

    [ValidateRange(1, 100000)]
    [int]$Samples = 120,

    [ValidateRange(1, 100000)]
    [int]$IntervalMs = 8
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build-steamvr"

$Calibrator = Get-ChildItem -Path $BuildDir -Filter "pico_ot_steamvr_calibrator.exe" -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $Calibrator) {
    throw "pico_ot_steamvr_calibrator.exe was not found. Run .\scripts\build_steamvr_driver.ps1 first."
}

Write-Host "Calibrator: $Calibrator"

switch ($Action) {
    "List" {
        & $Calibrator --list
    }
    "Clear" {
        & $Calibrator --clear
    }
    "Calibrate" {
        if ([string]::IsNullOrWhiteSpace($TrackerSerial)) {
            throw "-TrackerSerial is required for -Action Calibrate. Run -Action List first."
        }

        $controllerArg = $Controller.ToLowerInvariant()
        & $Calibrator `
            --tracker $TrackerSerial `
            --controller $controllerArg `
            --samples $Samples `
            --interval-ms $IntervalMs
    }
}

if ($LASTEXITCODE -ne 0) {
    throw "SteamVR translation calibrator failed with exit code $LASTEXITCODE."
}
