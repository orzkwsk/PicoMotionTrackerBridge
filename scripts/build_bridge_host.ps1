param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build-bridge-host"

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

Write-Host ""
Write-Host "Bridge host build/tests complete."
Write-Host "Receiver CLI: $ReceiverCli"
Write-Host "Run it with:"
Write-Host ('& "{0}" 29765' -f $ReceiverCli)
