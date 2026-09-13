param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$ProjectDir = Join-Path $RepoRoot "integrations\monaka"
$BuildDir = Join-Path $RepoRoot "build-monaka-receiver-capi"

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
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

& $cmake -S $ProjectDir -B $BuildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

& $cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Monaka receiver C API build failed." }

$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
if (-not (Test-Path $ctest)) {
    throw "ctest.exe was not found next to cmake.exe."
}

& $ctest --test-dir $BuildDir -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Monaka receiver C API CTest failed." }

$library = Get-ChildItem -Path $BuildDir -Filter "pico_ot_bridge_capi.dll" -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $library) {
    throw "pico_ot_bridge_capi.dll was not found after build."
}

Write-Host ""
Write-Host "Monaka receiver C API build/tests complete." -ForegroundColor Green
Write-Host "Native library: $library"
