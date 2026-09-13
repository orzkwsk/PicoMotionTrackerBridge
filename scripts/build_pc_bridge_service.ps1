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

$existingExe = Get-ChildItem -Path $BuildDir -Filter "pico_ot_bridge_service.exe" -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "\\$Config\\" } |
    Select-Object -First 1

$sourcePaths = @(
    (Join-Path $RepoRoot "CMakeLists.txt"),
    (Join-Path $RepoRoot "tools\pc_bridge_service.cpp"),
    (Join-Path $RepoRoot "include\pico_ot_bridge\service_contract.hpp"),
    (Join-Path $RepoRoot "src\bridge\receiver.cpp"),
    (Join-Path $RepoRoot "src\bridge\publisher.cpp"),
    (Join-Path $RepoRoot "src\bridge\protocol.cpp"),
    (Join-Path $RepoRoot "src\bridge\clock_sync.cpp"),
    (Join-Path $RepoRoot "src\bridge\udp_socket.cpp")
)

$needsBuild = -not $existingExe
if (-not $needsBuild) {
    foreach ($path in $sourcePaths) {
        if ((Test-Path $path) -and (Get-Item $path).LastWriteTimeUtc -gt $existingExe.LastWriteTimeUtc) {
            $needsBuild = $true
            break
        }
    }
}

if ($needsBuild) {
    $running = Get-Process -Name "pico_ot_bridge_service" -ErrorAction SilentlyContinue
    if ($running) {
        throw "PC Bridge Service is running but its binary needs an update. Exit the running service/process and retry."
    }

    & $cmake -S $RepoRoot -B $BuildDir `
        -DPICO_OT_BUILD_STEAMVR_DRIVER=OFF `
        -DPICO_OT_BUILD_TESTS=ON `
        -DPICO_OT_BUILD_SHARED=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    & $cmake --build $BuildDir --config $Config --target pico_ot_bridge_service
    if ($LASTEXITCODE -ne 0) { throw "PC Bridge Service build failed." }
}

$exe = Get-ChildItem -Path $BuildDir -Filter "pico_ot_bridge_service.exe" -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "\\$Config\\" } |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $exe) {
    throw "pico_ot_bridge_service.exe was not found after build."
}

Write-Output $exe
