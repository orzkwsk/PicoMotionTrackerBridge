# PicoMotionTrackerBridge

Version **0.1**

Native PICO Motion Tracker bridge derived from the `Pico4Ultra_ResearchTools` native-tracker investigation.

The project is split into a firmware-sensitive PICO backend and output/control layers. The backend normalizes PICO tracker state; consumers such as the SteamVR driver operate above that boundary.

## Development branches

- `main` — stable/release baseline.
- `dev` — current development integration branch and the normal base for new work.
- `feature/*`, `fix/*`, `refactor/*`, `diagnostic/*` — topic branches; normally branch from current `dev` and merge back to `dev`.
- Older coordinated branches such as `refactor/monaka-layer-separation` are retained as development history and are not the default base for new tasks.

Task-specific instructions may pin another branch or commit when reproducibility requires it. See `AGENTS.md` for Codex branch-handling rules.

## Current status

Version 0.1 establishes the complete PICO -> PC -> SteamVR bridge path used in the current hardware validation environment.

Validated areas include:

- private PICO tracking runtime access on the pinned runtime profile;
- full-serial tracker identity and five-tracker enumeration;
- simultaneous 6DoF acquisition, optical loss, reacquisition, known-device power-cycle recovery, and HMD sleep/resume recovery;
- normalized position/orientation/velocity state and dynamic timestamp mapping;
- HMD -> PC UDP transport with clock synchronization;
- SteamVR Generic Tracker registration and multi-tracker output;
- hardware-validated PICO -> OpenVR position and relative-orientation mapping;
- SteamVR linear-velocity submission for position prediction;
- output-quaternion-derived angular velocity for rotation prediction;
- common world-translation alignment for all PICO trackers;
- controller-reference translation calibration;
- manual XYZ adjustment at 1 mm steps, Shift+click 10 mm adjustment, Apply/Save/Load workflow, and unsaved-state warning.

The WPF control GUI migration is included in the current 0.1 codebase. The latest WPF reference-resolution fix should still receive a final local smoke test before treating that UI path as fully validated; the bridge/output behavior itself was already validated through the preceding control GUI implementation.

## Architecture

```text
PICO tracking service / per-tracker SHM
        ↓
libtrackingclient.pxr.so
        ↓
PICO Backend
  runtime access / decoding / identity / validity / timestamps
        ↓
Normalized TrackerState
================================ backend boundary
        ↓
HMD bridge publisher
        ↓ UDP + clock sync
PC bridge receiver
        ↓
SteamVR server driver
        ↓
Generic Tracker devices
```

Control path:

```text
WPF Control GUI
        ↓
world-alignment / diagnostic IPC
        ↓
SteamVR driver
```

A separate headless Control Service is the intended next integration step so the GUI and future MonakaVR Utility can become clients instead of talking to driver IPC directly.

## Runtime compatibility

`getBodyTrackerData()` is a recovered private ABI and is firmware-dependent.

Validated `libtrackingclient.pxr.so` GNU Build ID:

```text
d1b55bc0598ffff93cc01eaf089793f8
```

The recovered function is currently used at:

```text
libtrackingclient.pxr.so + 0x47dc8
```

Normal use should keep strict Build ID verification enabled. A different runtime Build ID must be treated as an unknown ABI until explicitly revalidated.

## Backend validity rules

`getBodyTrackerData() == 0` does not mean that optical position is valid.

Important normalized semantics:

```text
connectedSnapshot        <- enumeration-time TrackerInfo state
positionValid            <- algo_result_tracker byte +64
orientationSamplePresent <- sane quaternion sample may continue during optical loss
raw state +65            <- diagnostics only
```

Consumers must ignore position while `positionValid == false`, even if the raw position field changes during reacquisition.

The backend intentionally does not perform SteamVR coordinate conversion, body/mount transforms, IK, fusion, or application-specific tracker-role policy.

## Known topology behavior

```text
known tracker power-cycle
    -> ReinitializeRuntime()
    -> recovery

tracker absent when Backend starts
    -> powered on later
    -> ReinitializeRuntime() does not add it

Backend Stop/Start
    -> fresh enumeration
    -> newly powered tracker discovered
```

Repeated in-instance vendor enumeration remains disabled by default because allocator ownership across the vendor/runtime boundary has not been proven safe.

## Important prohibited paths

The backend must not:

- call `GetSwiftBattery(fullSerial, ...)`;
- fake or create a second OpenXR session for tracker access;
- use fabricated PICO XR instance/session handles;
- manually reset private SHM state;
- use runtime device IDs as persistent identity;
- bake SteamVR coordinate policy or physical mount transforms into the firmware adapter.

The full-serial `GetSwiftBattery` path is known to be capable of aborting the PICO tracking service and is intentionally not used.

## Build

Host tests / bridge utilities:

```powershell
.\scripts\build_bridge_host.ps1
```

SteamVR driver and calibrator:

```powershell
.\scripts\build_steamvr_driver.ps1
```

Register the SteamVR driver:

```powershell
.\scripts\register_steamvr_driver.ps1
```

Control GUI:

```powershell
.\scripts\steamvr_control_gui.ps1
```

The Android backend validation harness is under `BackendTestApp/`.

## Documentation

Detailed evidence and design notes are kept separately:

- `docs/ARCHITECTURE.md` — backend/output boundary and architecture;
- `docs/RUNTIME_VALIDATION.md` — backend hardware/runtime validation;
- `docs/STEAMVR_VALIDATION_STATUS.md` — SteamVR coordinate and prediction validation status;
- `docs/STEAMVR_COORDINATE_VALIDATION.md` — coordinate/alignment investigation;
- `docs/STEAMVR_CONTROL_SERVICE.md` — intended GUI/service separation;
- `docs/KNOWN_LIMITATIONS.md` — known constraints and provisional fields.

## Version 0.1 scope

Version 0.1 should be treated as the first complete bridge milestone rather than a polished end-user release. The core tracking path is established and hardware-validated. Remaining work is primarily integration/packaging: final WPF smoke testing, headless Control Service separation, startup/persistence orchestration, and MonakaVR Utility integration.
