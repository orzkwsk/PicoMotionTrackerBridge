# SteamVR control service boundary

This document defines the Windows-side control-plane boundary for the PICO OT SteamVR output.

## Goal

Keep the SteamVR runtime service path and the user-facing GUI independently runnable and independently replaceable.

The control GUI must not directly depend on the SteamVR driver IPC. A future MonakaVR / Utility application, CLI, or another frontend should be able to reuse the same control service without embedding the current GUI.

## Process topology

```text
PICO HMD bridge sender
        |
        v
SteamVR server driver (vrserver.exe)
  - receives pose stream
  - publishes Generic Trackers
  - owns only driver-local runtime state
  - exposes narrow local driver-control IPC
        ^
        | driver-local IPC
        |
PICO OT Control Service
  - sole normal client of driver-control IPC
  - owns persistent world-alignment settings
  - reapplies saved settings when driver IPC becomes available
  - performs / coordinates calibration
  - forwards diagnostic controls
  - exposes stable frontend IPC
        ^
        | local Named Pipe
        +----------------------+----------------------+
        |                      |                      |
        v                      v                      v
Standalone GUI             CLI / tests       future MonakaVR Utility
```

## Independence requirements

### SteamVR driver

The pose path must continue to operate when the control service and GUI are not running.

If no control service is present, the driver uses its built-in neutral control state (currently zero world translation). Loss or restart of the GUI/service must never disconnect tracker pose delivery.

The driver must not know about:

- WinForms or any GUI technology;
- config-file locations;
- MonakaVR / Utility UX;
- user-facing calibration workflows;
- frontend process lifetime.

### Control service

The control service is a normal user-space Windows process first. It must not require a visible window.

It owns:

- current desired world translation;
- persistence of alignment settings;
- driver-control IPC adaptation;
- driver availability detection / re-application after SteamVR restart;
- translation calibration orchestration;
- orientation-zero / position-zero diagnostic commands;
- stable frontend IPC.

Installing it as a Windows Service is explicitly *not* required for the first implementation. The service boundary refers to process responsibility, not SCM installation. This avoids session / OpenVR interaction problems while preserving later migration options.

### GUI

The GUI is a thin frontend. It must not open the driver's memory-mapped state or driver events directly.

It may only:

- connect to the control service;
- read service status/current settings;
- request alignment changes;
- request calibration;
- request diagnostic commands;
- display results/errors.

Closing the GUI must not stop the control service or tracker output.

## Frontend IPC

Use a local Windows Named Pipe as the stable frontend boundary.

Initial pipe name:

```text
PicoOTBridge.Control.v1
```

The first protocol should remain small and versioned. A line-delimited JSON request/response format is acceptable for the first implementation because traffic is low-rate control data rather than pose data and it is straightforward to consume from C#, C++, PowerShell, and a future Utility layer.

Initial operations:

```text
get_status
get_alignment
set_alignment
reset_alignment
save_alignment
reload_alignment
calibrate_translation
orientation_zero
orientation_clear
position_zero
position_clear
```

The service should return explicit success/error responses for every request.

## Persistence

The control service owns persistence, not the GUI.

Current location may remain:

```text
%LOCALAPPDATA%\PicoMotionTrackerBridge\steamvr_alignment.json
```

The GUI must not read or write this file directly once the service implementation is active.

On startup the service loads persisted alignment. When the SteamVR driver IPC becomes available, the service applies the desired alignment. If `vrserver.exe` restarts, the service reapplies it when the driver IPC reappears.

## Calibration

The existing `pico_ot_steamvr_calibrator.exe` remains a temporary internal implementation detail.

First service implementation may spawn it and then read/persist the resulting driver alignment. Long-term, the OpenVR calibration logic should be moved into a reusable library or directly into the service so calibration does not bypass the service state model.

The GUI must never invoke the calibrator directly once service separation is active.

## Migration target

This split is intentionally compatible with MonakaVR / Utility integration:

```text
current standalone GUI -> Named Pipe client
future Utility         -> Named Pipe client or shared service client library
control service        -> unchanged
SteamVR driver         -> unchanged
```

The service may later be generalized beyond SteamVR-specific controls, but the current implementation must not broaden its scope into pose fusion, IK, tracker-role assignment, or Backend private-ABI work.

## Implementation order

1. Add headless control-service executable and versioned Named Pipe request/response skeleton.
2. Move world-alignment read/write and persistence into the service.
3. Convert standalone GUI to a pure pipe client.
4. Add calibration command through the service.
5. Move diagnostics commands through the service.
6. Add reconnect / SteamVR-restart reapply behavior.
7. Only after validation, consider extracting a reusable client library for MonakaVR / Utility.
