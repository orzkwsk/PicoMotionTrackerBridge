param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "Build\runtime"

. (Join-Path $PSScriptRoot 'find_cmake.ps1')
$cmake = Find-InstalledCMake

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
$sourcePaths += Get-ChildItem -Path (Join-Path $RepoRoot "src\observation") -File -Recurse |
    Select-Object -ExpandProperty FullName
$sourcePaths += Get-ChildItem -Path (Join-Path $RepoRoot "include\pico_ot_backend") -Filter "observation_*.hpp" -File |
    Select-Object -ExpandProperty FullName
$sourcePaths += Get-ChildItem -Path (Join-Path $RepoRoot "include\pico_ot_bridge") -File -Recurse |
    Select-Object -ExpandProperty FullName
$sourcePaths += Get-ChildItem -Path (Join-Path $RepoRoot "third_party\monaka-protocol\cpp") -File -Recurse |
    Select-Object -ExpandProperty FullName

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
