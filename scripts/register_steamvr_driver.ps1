param(
    [string]$DriverPath = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DriverPath) {
    $DriverPath = Join-Path $RepoRoot "build-steamvr\steamvr\pico_ot_bridge"
}
$DriverPath = [System.IO.Path]::GetFullPath($DriverPath)

$manifest = Join-Path $DriverPath "driver.vrdrivermanifest"
$dll = Join-Path $DriverPath "bin\win64\driver_pico_ot_bridge.dll"
if (-not (Test-Path $manifest)) { throw "driver.vrdrivermanifest not found: $manifest" }
if (-not (Test-Path $dll)) { throw "driver DLL not found: $dll" }

$candidates = @()
try {
    $steamPath = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -ErrorAction Stop).SteamPath
    if ($steamPath) {
        $candidates += (Join-Path $steamPath "steamapps\common\SteamVR\bin\win64\vrpathreg.exe")
    }
} catch {}
$candidates += "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
$candidates += "C:\Program Files\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"

$vrpathreg = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vrpathreg) {
    throw "SteamVR vrpathreg.exe was not found."
}

Write-Host "vrpathreg: $vrpathreg"
Write-Host "Adding driver: $DriverPath"
& $vrpathreg adddriver $DriverPath
if ($LASTEXITCODE -ne 0) { throw "vrpathreg adddriver failed." }

Write-Host ""
Write-Host "Registered SteamVR driver. Restart SteamVR/vrserver before validation."
