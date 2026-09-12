param()

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$GuiSource = Join-Path $RepoRoot "tools\steamvr_control_gui.cs"

if (-not (Test-Path $GuiSource)) {
    throw "SteamVR control GUI source was not found: $GuiSource"
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not ("PicoOTBridge.SteamVrControlGui" -as [type])) {
    Add-Type -Path $GuiSource -ReferencedAssemblies @(
        "System.dll",
        "System.Core.dll",
        "System.Windows.Forms.dll",
        "System.Drawing.dll"
    )
}

[PicoOTBridge.SteamVrControlGui]::Run($RepoRoot)
