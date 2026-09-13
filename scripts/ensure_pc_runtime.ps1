param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$DriverDll = Join-Path $RepoRoot "build-steamvr\steamvr\pico_ot_bridge\bin\win64\driver_pico_ot_bridge.dll"

$driverSources = @(
    (Join-Path $RepoRoot "CMakeLists.txt")
)
$driverSources += Get-ChildItem -Path (Join-Path $RepoRoot "outputs\steamvr\driver") -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty FullName
$driverSources += Get-ChildItem -Path (Join-Path $RepoRoot "outputs\steamvr\common") -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty FullName
$driverSources += Get-ChildItem -Path (Join-Path $RepoRoot "src\bridge") -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty FullName
$driverSources += Get-ChildItem -Path (Join-Path $RepoRoot "include\pico_ot_bridge") -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty FullName

$needsDriverBuild = -not (Test-Path $DriverDll)
if (-not $needsDriverBuild) {
    $driverTime = (Get-Item $DriverDll).LastWriteTimeUtc
    foreach ($source in $driverSources | Select-Object -Unique) {
        if ((Test-Path $source) -and (Get-Item $source).LastWriteTimeUtc -gt $driverTime) {
            $needsDriverBuild = $true
            break
        }
    }
}

if ($needsDriverBuild) {
    $vrserver = Get-Process -Name "vrserver" -ErrorAction SilentlyContinue
    if ($vrserver) {
        throw "SteamVR driver needs an update, but vrserver.exe is using the current DLL. Exit SteamVR completely and run Start_All.cmd again."
    }

    $bridgeService = Get-Process -Name "pico_ot_bridge_service" -ErrorAction SilentlyContinue
    if ($bridgeService) {
        throw "Bridge binaries need an update, but PC Bridge Service is running. Exit pico_ot_bridge_service.exe and run Start_All.cmd again."
    }

    Write-Host "SteamVR driver is missing or stale. Building current sources..."
    & (Join-Path $PSScriptRoot "build_steamvr_driver.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) {
        throw "SteamVR driver build failed."
    }
} else {
    Write-Host "SteamVR driver is current."
}

# Keep the local bridge service binary current as part of the same one-click
# startup contract. This is cheap when the target is already up to date.
$serviceExe = & (Join-Path $PSScriptRoot "build_pc_bridge_service.ps1") -Config $Config |
    Select-Object -Last 1
if (-not $serviceExe -or -not (Test-Path $serviceExe)) {
    throw "PC Bridge Service build verification failed."
}

# adddriver is idempotent for the same path and covers first-run registration.
& (Join-Path $PSScriptRoot "register_steamvr_driver.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "SteamVR driver registration failed."
}

Write-Host "PC bridge runtime is ready."
