param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build-steamvr"

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\CMake\bin\cmake.exe"
    )
    $cmake = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $cmake) {
    throw "cmake.exe was not found."
}

Write-Host "CMake: $cmake"
Write-Host "Build: $BuildDir"

& $cmake -S $RepoRoot -B $BuildDir `
    -DPICO_OT_BUILD_STEAMVR_DRIVER=ON `
    -DPICO_OT_BUILD_TESTS=ON `
    -DPICO_OT_BUILD_SHARED=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

& $cmake --build $BuildDir --config $Config --target `
    pico_ot_steamvr_driver pico_ot_bridge_receiver_cli
if ($LASTEXITCODE -ne 0) { throw "SteamVR driver build failed." }

& $cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Host test build failed." }

$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
if (Test-Path $ctest) {
    & $ctest --test-dir $BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed." }
}

$PackageDir = Join-Path $BuildDir "steamvr\pico_ot_bridge"
$DriverDll = Join-Path $PackageDir "bin\win64\driver_pico_ot_bridge.dll"
if (-not (Test-Path $DriverDll)) {
    throw "Driver DLL was not produced at expected path: $DriverDll"
}

$ReceiverCli = Get-ChildItem -Path $BuildDir -Filter "pico_ot_bridge_receiver_cli.exe" -Recurse |
    Select-Object -First 1 -ExpandProperty FullName

Write-Host ""
Write-Host "SteamVR driver package: $PackageDir"
Write-Host "Driver DLL: $DriverDll"
if ($ReceiverCli) {
    Write-Host "Receiver CLI: $ReceiverCli"
}
Write-Host "Build/test complete."
