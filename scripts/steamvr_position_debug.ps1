param(
    [ValidateSet("Zero", "Clear")]
    [string]$Action = "Zero"
)

$ErrorActionPreference = "Stop"

$eventName = switch ($Action) {
    "Zero"  { "Local\PicoOTBridge_PositionZero" }
    "Clear" { "Local\PicoOTBridge_PositionClear" }
}

try {
    $event = [System.Threading.EventWaitHandle]::OpenExisting($eventName)
} catch [System.Threading.WaitHandleCannotBeOpenedException] {
    throw "PICO OT SteamVR driver position diagnostic event was not found. Rebuild the driver and restart SteamVR first."
}

try {
    if (-not $event.Set()) {
        throw "Failed to signal diagnostic event: $eventName"
    }

    if ($Action -eq "Zero") {
        Write-Host "Requested position zero for all PICO OT bridge trackers."
        Write-Host "The next fresh valid position becomes the per-tracker origin."
    } else {
        Write-Host "Cleared position zero for all PICO OT bridge trackers."
        Write-Host "Normal absolute PICO OutputA positions are restored."
    }
} finally {
    $event.Dispose()
}
