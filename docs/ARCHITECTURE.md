# Architecture

## Responsibility boundary

`PicoMotionTrackerBackend` owns only PICO device/runtime concerns:

```text
TrackingClient ABI
  -> runtime compatibility guard
  -> device enumeration / identity
  -> per-tracker pose read
  -> raw validity/state preservation
  -> SI unit normalization
  -> source/local timestamp mapping
```

It does not own body assignment, IK, fallback policy, reacquisition blending, SteamVR virtual-device creation, or SlimeVR-specific types.

## Layering

### `IPicoRuntimeAdapter`

Firmware/runtime-sensitive layer. All offsets, private symbols, private `std::__1` ABI assumptions and SHM-facing behavior stay here.

### `Backend`

Firmware-independent orchestration. Owns the serial-keyed device registry, source-clock mapping, and Raw -> SI normalization.

### C ABI

`pico_ot_backend.h` prevents the private vendor C++ ABI from leaking into wrapper/bridge code and makes the backend suitable for JNI, a native Android service, or another language FFI.

## Validity model

Connection and tracking quality are intentionally orthogonal:

```text
TrackerInfo connected=1, positionValid=1  -> optical 6DoF available
TrackerInfo connected=1, positionValid=0  -> optical position lost; orientation sample may continue
TrackerInfo connected=0                   -> device disconnected regardless of pose rc/timestamp
```

The bridge should implement any debounce/reacquisition policy. The backend only preserves component state.

## Coordinate frames

The initial backend reports Output A in `ReferenceFrame::PicoOutputA` and only performs unit normalization. The exact PICO->Monaka basis transform and fixed physical mount transform remain separate consumer-configurable transforms because multiplication side/direction has not yet been proven conclusively.
