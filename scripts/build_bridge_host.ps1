param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "Build\runtime"

. (Join-Path $PSScriptRoot 'find_cmake.ps1')
$cmake = Find-InstalledCMake

& $cmake -S $RepoRoot -B $BuildDir `
    -DPICO_OT_BUILD_STEAMVR_DRIVER=OFF `
    -DPICO_OT_BUILD_TESTS=ON `
    -DPICO_OT_BUILD_SHARED=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

& $cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Host build failed." }

$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
if (-not (Test-Path $ctest)) {
    throw "ctest.exe was not found next to cmake.exe."
}

& $ctest --test-dir $BuildDir -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest failed." }

$ReceiverCli = Get-ChildItem -Path $BuildDir -Filter "pico_ot_bridge_receiver_cli.exe" -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $ReceiverCli) {
    throw "pico_ot_bridge_receiver_cli.exe was not found after build."
}

$BridgeService = Get-ChildItem -Path $BuildDir -Filter "pico_ot_bridge_service.exe" -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $BridgeService) {
    throw "pico_ot_bridge_service.exe was not found after build."
}

Write-Host ""
Write-Host "Bridge host build/tests complete."
Write-Host "Receiver CLI: $ReceiverCli"
Write-Host "PC Bridge Service: $BridgeService"
Write-Host "HMD ingress port: 29765"
Write-Host "SteamVR fan-out port: 29766"
Write-Host "Monaka fan-out port: 29767"
Write-Host "Utility fan-out port: 29768"
