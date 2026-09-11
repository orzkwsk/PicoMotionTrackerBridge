# PicoMotionTrackerBridge

Native PICO Motion Tracker bridge project derived from the `Pico4Ultra_ResearchTools` native-tracker investigation.

The repository is intentionally layered:

- the **backend core** owns PICO-specific runtime access, decoding, device identity, validity semantics, unit normalization, and timestamp mapping;
- **bridge/output modules** consume that normalized state and expose it to systems such as SteamVR or MonakaVR.

The current `feature/pico-ot-backend` branch implements and validates the backend core plus a minimal Android test harness. SteamVR output is the next bridge layer and is not implemented yet.

## Source of truth

Implementation baseline:

- Research repository: `orzkwsk/Pico4Ultra_ResearchTools`
- Branch: `investigation/native-tracker-access`
- Baseline commit: `a0af5de5ef3c5dee686eba289fbb7a38fef2a654`
- Primary specification: `NativeTrackerProbe/docs/PICO_OT_BRIDGE_HANDOFF.md`
- Probe baseline: `0.1.0-dev.26`

## Data path

```text
PICO tracking service / per-tracker SHM
        ↓
libtrackingclient.pxr.so
        ↓
CurrentFirmwareRuntimeAdapter
        ↓
RawTrackerState (PICO units / flags)
        ↓
Backend normalization
        ↓
TrackerState (SI units, PICO Output-A frame)
        ↓
Bridge output / integration
        ├─ SteamVR generic tracker        [planned]
        ├─ MonakaVR input adapter         [planned]
        └─ other consumers / IPC          [future]
```

The backend runtime path does not create an OpenXR instance/session and is designed to coexist with Virtual Desktop owning the XR session.

## Backend contract

- Persistent identity: full tracker serial number.
- Runtime device ID: metadata only; never used as persistent identity.
- Output A pose decoding:
  - source timestamp `+0x000`
  - position `+0x008` (mm -> m)
  - quaternion `+0x020` (XYZW)
  - position validity `+64`
  - raw vendor quality/state `+65`
  - linear velocity `+0x0E0` (mm/s -> m/s)
  - linear acceleration candidate `+0x0F8` (mm/s^2 -> m/s^2, still semantically provisional)
  - angular velocity `+0x110` (rad/s; treated as device/local-frame candidate)
- TrackerInfo decoding:
  - connection/state `+0`
  - runtime deviceId `+4`
  - full serial `+18..35`
  - charging `+42`
  - raw battery bucket candidate `+43`
- Source timestamp and local monotonic observation timestamp are preserved separately.
- Source -> local timestamp mapping is dynamically rebased; no fixed timestamp offset is used.
- HMD/service lifecycle recovery entry point: `ReinitializeRuntime()` / `pico_ot_reinitialize_runtime()`.
- C++ API and stable C ABI are both exposed.

## Important validity semantics

`getBodyTrackerData() == 0` is **not** a connection or positional-validity test.

Validated on PICO 4 Ultra on 2026-09-11:

```text
connected       <- TrackerInfo connection snapshot
positionValid   <- algo_result_tracker byte +64
orientation     <- sane quaternion sample can continue during optical loss
raw state +65   <- diagnostics only; not a validity flag
```

Observed optical-loss behavior includes frozen position and zero linear velocity while orientation/angular velocity continue. However, during reacquisition the vendor position field can change **while `positionValid == 0`**. Consumers must therefore ignore position whenever `positionValid` is false; they must not infer validity from whether position is changing or frozen.

The raw byte at `+65` has been observed as `0`, `1`, `2`, and `4`, including `state65=0` while `positionValid=1`. It is preserved for diagnostics only.

The backend deliberately names the orientation flag `orientationSamplePresent`, not `orientationValid`, because a universal vendor orientation-valid flag has not been recovered.

## Runtime compatibility guard

`getBodyTrackerData()` was recovered at `libtrackingclient.pxr.so + 0x47dc8` for the currently validated runtime. This is not a public ABI.

Validated GNU Build ID:

```text
d1b55bc0598ffff93cc01eaf089793f8
```

Production/normal bridge use should pin this Build ID through `expectedLibraryBuildIdHex` / `expected_library_build_id_hex` and keep `allowUnverifiedRuntime=false`. Research against a new firmware may explicitly enable the unverified-runtime override only long enough to identify and validate the new runtime.

Do not silently carry the offset to a new PICO firmware build.

## Tracker enumeration allocator limitation

`getSwiftTrackerConnectState(std::vector<TrackerInfo>&)` allocates the vector buffer inside vendor/system libc++. The research probe intentionally did not free this buffer because cross-runtime allocator ownership has not been proven safe.

The backend preserves that safety rule:

- initial enumeration is allowed once;
- repeated enumeration is **disabled by default**;
- the bounded leaked allocation is reported in `RuntimeDiagnostics::intentionallyLeakedEnumerationBytes`;
- `allowRepeatedLeakyEnumeration` exists only as an explicit investigation escape hatch.

On the validated runtime the one-time enumeration reports `360` intentionally leaked bytes, consistent with a five-entry `TrackerInfo` allocation at 72 bytes per entry. This is allocation capacity, not the number of connected trackers.

A production long-running connection-state poller still needs a proven vendor-compatible vector destruction/free path, Binder-level enumeration, or another no-leak API/callback.

## Mandatory prohibited paths

The backend core does not and must not:

- call `GetSwiftBattery(fullSerial, ...)`;
- create/fake an OpenXR session;
- call `PICO_OnInstanceCreate` / `PICO_OnSessionCreate` with fabricated handles;
- use `Pxr_SetUserDefinedSettings()` as bootstrap;
- manually call `newTrackerClient::setShareMemoryFD()` / `resetShareMemory()`;
- change tracker pairing/tracking mode;
- perform reset/calibration from the raw reader;
- use runtime deviceId as persistent identity;
- bake the measured A->B transform into the runtime reader;
- bake body/device mount correction into the raw runtime adapter.

The `GetSwiftBattery` full-serial path is known to abort `pvrtrackingservice` through an uncaught `std::invalid_argument` and is intentionally absent.

## Validation status

Completed:

- Windows/MSVC host build;
- decoder/clock CTest;
- Android arm64-v8a NDK build;
- APK install/start on PICO 4 Ultra;
- strict runtime path execution through `libtrackingclient.pxr.so`;
- two-tracker enumeration;
- continuous pose polling;
- optical loss and reacquisition;
- IMU orientation/angular-velocity continuation while position is invalid;
- simultaneous two-tracker 6DoF operation;
- dynamic source-to-monotonic timestamp mapping.

Still to validate before treating the bridge runtime as production-ready:

- 60/120 Hz polling and actual vendor sample cadence;
- HMD sleep/resume + `ReinitializeRuntime()` recovery;
- disconnect/reconnect behavior over a long-lived process;
- Virtual Desktop coexistence regression;
- allocator-safe dynamic device refresh;
- SteamVR output layer.

See `docs/RUNTIME_VALIDATION.md` for observed runtime semantics.

## Build

Host-side decoder/clock tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Android runtime target: arm64-v8a / API 29 or later. Build with the Android NDK CMake toolchain; the Android target links `dl` and `log` and includes the AArch64 singleton-call shim.

The repository also contains `BackendTestApp`, a non-XR Android validation harness used before connecting the backend to an output module.

## C ABI quick start

```c
pico_ot_config_t cfg;
pico_ot_default_config(&cfg);

cfg.expected_library_build_id_hex = "d1b55bc0598ffff93cc01eaf089793f8";
cfg.allow_unverified_runtime = 0;

pico_ot_backend_t* backend = pico_ot_create();
if (pico_ot_start(backend, &cfg) == PICO_OT_OK) {
    size_t n = pico_ot_device_count(backend);
    for (size_t i = 0; i < n; ++i) {
        pico_ot_device_info_t dev;
        pico_ot_get_device(backend, i, &dev);

        pico_ot_pose_t pose;
        pico_ot_poll_pose(backend, dev.serial, &pose);

        if (pose.position_valid) {
            // Position may be consumed.
        }
        // orientation_sample_present is intentionally independent of position_valid.
    }
}
pico_ot_destroy(backend);
```

## Bridge layering

The backend core intentionally remains usable without SteamVR, but this repository is the bridge project. Output-specific policy should be added as separate modules rather than folded into the private PICO runtime adapter.

```text
backend/
  PICO runtime + normalized tracker state

outputs/
  steamvr/       [next]
  ...

integrations/
  monaka/        [future]
```

This keeps PICO firmware-sensitive code isolated while still allowing the repository to provide a complete standalone PICO-to-SteamVR bridge.