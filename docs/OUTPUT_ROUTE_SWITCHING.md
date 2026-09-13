# Output route switching

This document defines the Windows-side route switch between the native SteamVR direct output and an external consumer such as MonakaVR.

## Why a route switch is required

The SteamVR server driver and the Monaka receiver C API both consume the same PICO bridge UDP stream on port `29765`.

Only one receiver can own that port at a time. The route switch therefore controls receiver ownership rather than merely hiding SteamVR devices in the GUI.

## Routes

### SteamVR Direct

```text
PICO HMD Backend / publisher
    -> UDP 29765
    -> native SteamVR server driver
    -> Generic Tracker devices
```

The SteamVR driver owns UDP `29765` and publishes the tracker state directly to SteamVR.

### MonakaVR / External

```text
PICO HMD Backend / publisher
    -> UDP 29765
    -> MonakaVR / external receiver
```

The SteamVR driver stops its receiver and releases UDP `29765`. Existing SteamVR Generic Tracker device objects remain registered for the lifetime of `vrserver`, but their poses become disconnected/invalid while this route is selected.

The external consumer is then free to bind the bridge port. The existing Monaka receiver C API is the intended first consumer.

## Persistence and runtime control

The selected route is persisted in:

```text
%LOCALAPPDATA%\PicoMotionTrackerBridge\output_route.txt
```

Supported values:

```text
steamvr-direct
monaka-external
```

If the file is missing, unreadable, or contains an unknown value, the driver defaults to `steamvr-direct` for backward compatibility.

The tray host signals a named event after changing the persisted route:

```text
Local\PicoOTBridge_OutputRouteUpdated
```

The SteamVR driver reloads the route when it receives this event.

## Switching back to SteamVR Direct

If the external receiver still owns UDP `29765`, the SteamVR driver stays loaded and retries the bind periodically instead of failing the whole driver initialization. The current retry interval is approximately one second.

For a deterministic handover, stop/release the external receiver before selecting `SteamVR Direct`.

## GUI/tray behavior

`scripts/steamvr_control_gui.ps1` builds one Windows executable containing:

- a persistent notification-area/tray host;
- the existing WPF SteamVR control window.

Normal launch starts the tray host and opens the control window once. Closing the control window does not stop the tray host. The window can be reopened from the tray icon or by double-clicking the icon.

Tray output-route choices:

```text
SteamVR Direct
MonakaVR / External
```

The existing WPF alignment/calibration/diagnostic functionality is unchanged and remains a separate window.

## Scope

This route switch does not modify the validated PICO Backend contract, packet protocol, tracker identity, pose validity semantics, or Monaka receiver C API. It only selects which Windows-side receiver owns the PICO UDP stream.
