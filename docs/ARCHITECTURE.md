# Architecture

## Project boundary

`PicoMotionTrackerBridge` is the complete bridge project. Internally it is split into a completed PICO-specific backend core and higher-level output/integration modules.

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

The repository name describes the end product. `Backend` remains the low-level PICO input component name.

The standalone Backend phase is validated and closed on the currently pinned runtime profile. Output/integration work begins above the normalized tracker-state boundary.

## Backend responsibility

The backend owns only PICO device/runtime concerns:

```text
TrackingClient ABI
  -> runtime compatibility guard
  -> startup device enumeration / identity
  -> per-tracker pose read
  -> raw validity/state preservation
  -> SI unit normalization
  -> source/local timestamp mapping
  -> known-device runtime reinitialization
```

The backend does **not** own:

- body assignment;
- IK;
- fallback/fusion policy;
- reacquisition blending;
- mount/body correction;
- SteamVR virtual-device creation;
- SlimeVR-specific types;
- HMD recenter/playspace offsets.

Those belong above the normalized tracker-state boundary.

## Bridge responsibility

The bridge layer consumes `TrackerState` and decides how to expose it to another system.

Expected layering:

```text
src/runtime/                    firmware-sensitive PICO private ABI
src/backend / Backend           normalized PICO tracker state
outputs/steamvr/                SteamVR-specific device/provider logic
integrations/monaka/            MonakaVR-facing adapter
```

Dependency direction remains one-way: output/integration modules depend on the backend contract, never the reverse.

## Backend layers

### `IPicoRuntimeAdapter`

Firmware/runtime-sensitive layer. All offsets, private symbols, private `std::__1` ABI assumptions and SHM-facing behavior stay here.

### `Backend`

Firmware-independent orchestration. Owns:

- serial-keyed device registry;
- source-clock mapping;
- Raw -> SI normalization;
- lifecycle/reinitialize control;
- Backend-visible device metadata.

The current safe device registry is startup-enumerated. `ReinitializeRuntime()` can recover a previously registered tracker but does not discover a serial that was absent at Backend start. Topology refresh therefore requires Backend Stop/Start under the default safe policy.

### C ABI

`pico_ot_backend.h` prevents the private vendor C++ ABI from leaking into JNI, SteamVR integration, MonakaVR integration, or another language FFI.

## Backend contract

The normalized state boundary is intentionally narrow. A consumer can rely on these concepts:

```text
serial                      persistent identity
runtimeDeviceId             session/runtime metadata only
position                    meters, PicoOutputA
orientation                 quaternion XYZW, PicoOutputA
linearVelocity              m/s
angularVelocity             rad/s, frame still provisional
positionValid               authoritative positional-use gate
orientationSamplePresent    sane quaternion sample present
sourceTimestamp             vendor/query clock domain
observedMonotonicNs         local CLOCK_MONOTONIC observation/query time
mappedMonotonicNs           mapped local monotonic time
mappedMonotonicValid        timestamp mapping validity
rawTrackingState65          diagnostics only
referenceFrame              PicoOutputA
```

## Validity model

Connection snapshot, positional validity, orientation sample presence, and vendor raw state are intentionally orthogonal.

Observed states include:

```text
connectedSnapshot=1
positionValid=0
orientationSamplePresent=1
```

`getBodyTrackerData() == 0` is not a connection or positional-validity test.

`connectedSnapshot` is the startup enumeration result and is not a live physical-connection signal.

### Invalid position semantics

A false `positionValid` does not imply that the numeric position field is frozen.

Observed behavior:

```text
optical loss
  -> position can freeze
  -> linear velocity commonly becomes zero
  -> orientation/angular velocity continue

reacquisition in progress
  -> position field can change while positionValid remains false
```

Therefore any output module must gate positional use strictly on `positionValid`.

Reacquisition debounce/blending, if desired, belongs in the bridge/output layer. The backend only preserves component state.

### Orientation semantics

No universal vendor orientation-valid bit has been recovered. `orientationSamplePresent` only means that a sane quaternion sample was present. Higher layers may apply their own liveness/quality policy.

### Raw state +65

The raw byte at vendor offset `+65` has been observed as `0`, `1`, `2`, `3`, and `4`, including `0` while `positionValid=1`. It remains diagnostic-only.

## Runtime identity

The validated PICO runtime profile is:

```text
libtrackingclient.pxr.so GNU Build ID:
d1b55bc0598ffff93cc01eaf089793f8

getBodyTrackerData offset:
0x47dc8

algo_result_tracker size:
392 bytes
```

Normal use pins this Build ID. A firmware update producing another Build ID is treated as an unknown ABI until explicitly validated.

## Device discovery and lifecycle

Validated lifecycle rules:

```text
known tracker power-cycle
    -> ReinitializeRuntime()
    -> tracker map rebuilt
    -> same serial resumes

new tracker absent at Backend startup
    -> power on later
    -> ReinitializeRuntime()
    -> not added to Backend registry

Backend Stop/Start
    -> fresh TrackerInfo enumeration
    -> new tracker discovered
```

Repeated enumeration is disabled by default because the vendor/system libc++ vector-buffer ownership is unresolved. The one-time allocation is intentionally leaked and reported through diagnostics.

## Coordinate frames

The backend reports Output A as `ReferenceFrame::PicoOutputA` and only performs unit normalization.

Hardware validation showed that PICO HMD recenter and OVR/SteamVR playspace movement do not apply the corresponding transform to the raw Output A tracker pose. Recenter may temporarily invalidate optical position and trigger normal reacquisition, but the tracker returns in the same raw reference frame.

Therefore:

- no HMD recenter compensation is applied in the backend;
- no OVR playspace transform is applied in the backend;
- PICO -> SteamVR/MonakaVR basis conversion belongs above the backend;
- physical mount/body transform belongs above the backend.

Quaternion hemisphere continuity (`q` versus `-q`) is also a consumer/output concern because both representations encode the same rotation.

## Timestamp model

The private call is queried with local `CLOCK_MONOTONIC` time. The returned vendor timestamp tracks that query time in a separate clock domain with a run-dependent offset.

Consequences:

- 60/120 Hz-class validation establishes API/query stability;
- it does not establish native sensor or optical-solver publication rate;
- dynamic source -> local mapping is retained because the clock-domain offset is not hard-coded.

## Completed hardware validation boundary

Validated on real PICO 4 Ultra hardware with the pinned runtime profile:

- private ABI guarded access;
- strict Build ID verification;
- full-serial identity;
- startup enumeration;
- 6DoF acquisition;
- optical loss and reacquisition;
- orientation/angular-velocity continuation while position is invalid;
- simultaneous two-tracker 6DoF;
- 60 Hz and 120 Hz-class query stress;
- reinitialize during valid tracking;
- known-tracker power-cycle recovery;
- HMD sleep/resume natural recovery;
- post-resume reinitialize;
- startup-only discovery behavior characterization;
- Backend-restart topology refresh;
- PICO recenter/raw-frame behavior;
- OVR playspace/raw-frame isolation;
- five-tracker enumeration;
- five-tracker simultaneous 6DoF;
- five-tracker 120 Hz-class query stress;
- five-tracker optical loss/reacquisition.

The standalone Backend phase is therefore complete for this runtime profile.

## Next architectural phase

The next phase starts above this boundary and should be implemented in a separate branch.

```text
completed PICO Backend
    ↓
coordinate conversion / output policy
    ↓
SteamVR generic tracker output
    ↓
MonakaVR integration / fusion / IK as separate higher layers
```

Backend code should not be modified to absorb SteamVR, IK, fusion, or body-assignment policy unless a newly discovered PICO-runtime requirement genuinely belongs below the normalized tracker-state boundary.
