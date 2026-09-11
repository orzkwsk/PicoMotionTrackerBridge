# Architecture

## Project boundary

`PicoMotionTrackerBridge` is the complete bridge project. Internally it is split into a PICO-specific backend core and output/integration modules.

```text
PICO tracking service
    ↓
PICO backend core
    ↓
Normalized TrackerState
    ↓
Bridge output / integration
    ├─ SteamVR output
    ├─ MonakaVR adapter
    └─ future IPC / protocol outputs
```

The repository name therefore describes the end product. `Backend` remains the low-level input component name.

## Backend responsibility

The backend owns only PICO device/runtime concerns:

```text
TrackingClient ABI
  -> runtime compatibility guard
  -> device enumeration / identity
  -> per-tracker pose read
  -> raw validity/state preservation
  -> SI unit normalization
  -> source/local timestamp mapping
```

The backend does **not** own body assignment, IK, fallback policy, reacquisition blending, SteamVR virtual-device creation, or SlimeVR-specific types. Those belong above the normalized tracker-state boundary.

## Bridge responsibility

The bridge layer may consume `TrackerState` and decide how to expose it to another system. For example, a SteamVR module can create generic tracker devices without teaching the PICO runtime adapter anything about SteamVR.

Expected layering:

```text
src/runtime/                    firmware-sensitive PICO private ABI
src/backend / Backend           normalized PICO tracker state
outputs/steamvr/                SteamVR-specific device/provider logic
integrations/monaka/            optional MonakaVR-facing adapter
```

The exact directory split may evolve, but dependency direction should remain one-way: output modules depend on the backend contract, never the reverse.

## Backend layers

### `IPicoRuntimeAdapter`

Firmware/runtime-sensitive layer. All offsets, private symbols, private `std::__1` ABI assumptions and SHM-facing behavior stay here.

### `Backend`

Firmware-independent orchestration. Owns the serial-keyed device registry, source-clock mapping, and Raw -> SI normalization.

### C ABI

`pico_ot_backend.h` prevents the private vendor C++ ABI from leaking into JNI, SteamVR integration, MonakaVR integration, or another language FFI.

## Validity model

Connection, positional validity, orientation sample presence, and vendor raw state are intentionally orthogonal:

```text
TrackerInfo connected=1, positionValid=1  -> optical position may be consumed
TrackerInfo connected=1, positionValid=0  -> position must not be consumed; orientation sample may continue
TrackerInfo connected=0                   -> device disconnected regardless of pose rc/timestamp
```

`getBodyTrackerData() == 0` is not a connection or positional-validity test.

The raw byte at vendor offset `+65` is diagnostic only. It has been observed as `0`, `1`, `2`, and `4`, including `0` while `positionValid=1`; no simple validity enum should be inferred from it.

### Invalid position semantics

A false `positionValid` does not imply that the numeric position field is frozen. Two behaviors have been observed:

```text
optical loss
  -> position can freeze
  -> linear velocity commonly becomes zero
  -> orientation/angular velocity continue

reacquisition in progress
  -> position field can change while positionValid remains false
```

Therefore any output module must gate positional use strictly on `positionValid`. Position changes while invalid are candidate/vendor-internal values and are not an alternate tracking source.

Reacquisition debounce/blending, if desired, belongs in the bridge/output layer. The backend only preserves component state.

## Runtime identity

The currently validated PICO runtime has:

```text
libtrackingclient.pxr.so GNU Build ID:
d1b55bc0598ffff93cc01eaf089793f8

getBodyTrackerData offset:
0x47dc8

algo_result_tracker size:
392 bytes
```

Normal use should pin the Build ID. A firmware update producing another Build ID must be treated as an unknown ABI until explicitly validated.

## Coordinate frames

The initial backend reports Output A in `ReferenceFrame::PicoOutputA` and only performs unit normalization. The exact PICO->Monaka/SteamVR basis transform and fixed physical mount transform remain above the raw runtime adapter because multiplication side/direction and final consumer conventions are separate concerns.

## Current validation boundary

Validated on real PICO 4 Ultra hardware:

- two tracker enumeration;
- continuous pose reads;
- 6DoF acquisition;
- optical loss;
- IMU orientation/angular velocity continuation;
- reacquisition;
- simultaneous two-tracker 6DoF;
- source-to-monotonic clock mapping.

Still pending at this layer are high-rate sample-cadence characterization, sleep/resume recovery, long-lived disconnect/reconnect handling, Virtual Desktop coexistence regression, and allocator-safe dynamic enumeration.