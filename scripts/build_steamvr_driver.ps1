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
    pico_ot_steamvr_driver pico_ot_steamvr_calibrator pico_ot_bridge_receiver_cli
if ($LASTEXITCODE -ne 0) { throw "SteamVR driver/calibrator build failed." }

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

$RenderModelScript = Join-Path $PSScriptRoot "generate_steamvr_rendermodel.ps1"
if (-not (Test-Path $RenderModelScript)) {
    throw "Render model generator was not found: $RenderModelScript"
}
& $RenderModelScript -DriverPackageDir $PackageDir
if ($LASTEXITCODE -ne 0) { throw "SteamVR render model generation failed." }

$RenderModelObj = Join-Path $PackageDir "rendermodels\pico_motion_tracker\pico_motion_tracker.obj"
$RenderModelMtl = Join-Path $PackageDir "rendermodels\pico_motion_tracker\pico_motion_tracker.mtl"
$RenderModelPng = Join-Path $PackageDir "rendermodels\pico_motion_tracker\pico_motion_tracker.png"
foreach ($RenderAsset in @($RenderModelObj, $RenderModelMtl, $RenderModelPng)) {
    if (-not (Test-Path $RenderAsset)) {
        throw "SteamVR render model asset was not produced: $RenderAsset"
    }
}

$ReceiverCli = Get-ChildItem -Path $BuildDir -Filter "pico_ot_bridge_receiver_cli.exe" -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
$Calibrator = Get-ChildItem -Path $BuildDir -Filter "pico_ot_steamvr_calibrator.exe" -Recurse |
    Select-Object -First 1 -ExpandProperty FullName

Write-Host ""
Write-Host "SteamVR driver package: $PackageDir"
Write-Host "Driver DLL: $DriverDll"
Write-Host "Render model: $RenderModelObj"
if ($ReceiverCli) {
    Write-Host "Receiver CLI: $ReceiverCli"
}
if ($Calibrator) {
    Write-Host "Calibrator: $Calibrator"
}
Write-Host "Build/test complete."
