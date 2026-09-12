param()

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$GuiSource = Join-Path $RepoRoot "tools\steamvr_control_gui.cs"
$GuiMainSource = Join-Path $RepoRoot "tools\steamvr_control_gui_main.cs"
$BuildDir = Join-Path $RepoRoot "build-gui"
$GuiExe = Join-Path $BuildDir "pico_ot_steamvr_control.exe"

foreach ($path in @($GuiSource, $GuiMainSource)) {
    if (-not (Test-Path $path)) {
        throw "SteamVR control GUI source was not found: $path"
    }
}

$cscCandidates = @(
    (Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"),
    (Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319\csc.exe")
)
$Csc = $cscCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Csc) {
    throw "C# compiler (csc.exe) was not found in the .NET Framework installation."
}

$needsBuild = -not (Test-Path $GuiExe)
if (-not $needsBuild) {
    $exeTime = (Get-Item $GuiExe).LastWriteTimeUtc
    foreach ($source in @($GuiSource, $GuiMainSource)) {
        if ((Get-Item $source).LastWriteTimeUtc -gt $exeTime) {
            $needsBuild = $true
            break
        }
    }
}

if ($needsBuild) {
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

    & $Csc `
        /nologo `
        /target:winexe `
        /optimize+ `
        /out:$GuiExe `
        /reference:System.dll `
        /reference:System.Core.dll `
        /reference:System.Windows.Forms.dll `
        /reference:System.Drawing.dll `
        $GuiSource `
        $GuiMainSource

    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $GuiExe)) {
        throw "Failed to build SteamVR control GUI executable."
    }
}

Write-Host "GUI: $GuiExe"
Start-Process -FilePath $GuiExe -ArgumentList @($RepoRoot)
