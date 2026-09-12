param(
    [ValidateSet("Zero", "Clear")]
    [string]$Action = "Zero"
)

$ErrorActionPreference = "Stop"

$eventName = switch ($Action) {
    "Zero"  { "Local\PicoOTBridge_OrientationZero" }
    "Clear" { "Local\PicoOTBridge_OrientationClear" }
}

try {
    $event = [System.Threading.EventWaitHandle]::OpenExisting($eventName)
} catch [System.Threading.WaitHandleCannotBeOpenedException] {
    throw "PICO OT SteamVR driver diagnostic event was not found. Rebuild the driver and restart SteamVR first."
}

try {
    if (-not $event.Set()) {
        throw "Failed to signal diagnostic event: $eventName"
    }

    if ($Action -eq "Zero") {
        Write-Host "Requested orientation zero for all PICO OT bridge trackers."
        Write-Host "The next fresh orientation sample becomes identity for each tracker."
    } else {
        Write-Host "Cleared orientation zero for all PICO OT bridge trackers."
        Write-Host "Normal absolute orientation output is restored."
    }
} finally {
    $event.Dispose()
}
