param(
    [ValidateSet("Show", "Hide", "Check")]
    [string]$Action = "Check"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$PackageDir = Join-Path $RepoRoot "build-steamvr\steamvr\pico_ot_bridge"
$VisibleObj = Join-Path $PackageDir "rendermodels\pico_motion_tracker\pico_motion_tracker.obj"
$VisibleMtl = Join-Path $PackageDir "rendermodels\pico_motion_tracker\pico_motion_tracker.mtl"
$VisiblePng = Join-Path $PackageDir "rendermodels\pico_motion_tracker\pico_motion_tracker.png"
$HiddenObj = Join-Path $PackageDir "rendermodels\pico_motion_tracker_hidden\pico_motion_tracker_hidden.obj"

function Signal-DriverEvent {
    param([string]$Name)

    $source = @"
using System;
using System.Threading;
public static class PicoOtRenderModelEvent {
    public static void Signal(string name) {
        try {
            using (var evt = EventWaitHandle.OpenExisting(name)) {
                if (!evt.Set()) throw new InvalidOperationException("Failed to signal " + name);
            }
        } catch (WaitHandleCannotBeOpenedException) {
            throw new InvalidOperationException("SteamVR PICO OT driver event was not found. Start SteamVR with the driver first.");
        }
    }
}
"@
    if (-not ("PicoOtRenderModelEvent" -as [type])) {
        Add-Type -TypeDefinition $source -Language CSharp
    }
    [PicoOtRenderModelEvent]::Signal($Name)
}

switch ($Action) {
    "Show" {
        Signal-DriverEvent "Local\PicoOTBridge_RenderModelShow"
        Write-Host "Requested PICO tracker render model: SHOW"
    }
    "Hide" {
        Signal-DriverEvent "Local\PicoOTBridge_RenderModelHide"
        Write-Host "Requested PICO tracker render model: HIDE"
    }
    "Check" {
        $items = @(
            @{ Name = "Visible OBJ"; Path = $VisibleObj },
            @{ Name = "Visible MTL"; Path = $VisibleMtl },
            @{ Name = "Visible PNG"; Path = $VisiblePng },
            @{ Name = "Hidden OBJ"; Path = $HiddenObj }
        )
        foreach ($item in $items) {
            $ok = Test-Path $item.Path
            Write-Host ("{0,-12}: {1}  {2}" -f $item.Name, $(if ($ok) { "OK" } else { "MISSING" }), $item.Path)
        }

        $steamPath = $null
        try {
            $steamPath = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction Stop).SteamPath
        } catch {
        }
        if ($steamPath) {
            $logPath = Join-Path $steamPath "logs\vrserver.txt"
            if (Test-Path $logPath) {
                Write-Host ""
                Write-Host "Recent pico_ot_bridge render-model log lines:"
                Get-Content $logPath -Tail 500 |
                    Select-String "pico_ot_bridge: render model|render-model controls|activated tracker" |
                    Select-Object -Last 30 |
                    ForEach-Object { $_.Line }
            }
        }
    }
}
