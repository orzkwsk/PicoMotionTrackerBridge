# PicoMotionTrackerBackend

Native, non-XR PICO Motion Tracker backend derived from the `Pico4Ultra_ResearchTools` native-tracker investigation.

The backend reads PICO Motion Tracker state through the internal `libtrackingclient.pxr.so` / `TrackingClient` path and intentionally contains **no SlimeVR, SteamVR, IK, body-role, or OpenXR-session ownership code**.

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
consumer wrapper / bridge
```

The first runtime adapter uses the research-proven `getBodyTrackerData()` Output A path. It does not create an OpenXR instance/session and is designed to coexist with Virtual Desktop owning the XR session.

## Implemented contract

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

`getBodyTrackerData() == 0` is **not** a connection test.

During optical loss the researched runtime keeps the tracker connected, freezes position, sets `+64` to zero, zeros linear velocity, and continues IMU-derived orientation/angular velocity. Therefore consumers should treat the sample as component-valid:

```text
connected       <- TrackerInfo connection snapshot
positionValid   <- algo_result_tracker byte +64
orientation     <- sample remains present during optical loss
raw state +65   <- diagnostics only; not the sole validity flag
```

The backend deliberately names the orientation flag `orientationSamplePresent`, not `orientationValid`, because a universal vendor orientation-valid flag has not been recovered.

## Runtime compatibility guard

`getBodyTrackerData()` was recovered at `libtrackingclient.pxr.so + 0x47dc8` for the research runtime. This is not a public ABI.

The adapter reads the ELF GNU Build ID of the loaded library. Production use should set `expectedLibraryBuildIdHex`; otherwise startup fails by default. Research/development can explicitly set `allowUnverifiedRuntime=true` / `allow_unverified_runtime=1` to use the recovered offset while recording the actual Build ID in runtime diagnostics.

Do not silently carry the offset to a new PICO firmware build.

## Tracker enumeration allocator limitation

`getSwiftTrackerConnectState(std::vector<TrackerInfo>&)` allocates the vector buffer inside vendor/system libc++. The research probe intentionally did not free this buffer because cross-runtime allocator ownership has not been proven safe.

This backend preserves that safety rule:

- initial enumeration is allowed once;
- repeated enumeration is **disabled by default**;
- the bounded leaked allocation is reported in `RuntimeDiagnostics::intentionallyLeakedEnumerationBytes`;
- `allowRepeatedLeakyEnumeration` exists only as an explicit investigation escape hatch.

A production long-running connection-state poller still needs a proven vendor-compatible vector destruction/free path, Binder-level enumeration, or another no-leak API/callback.

## Mandatory prohibited paths

The backend does not and must not:

- call `GetSwiftBattery(fullSerial, ...)`;
- create/fake an OpenXR session;
- call `PICO_OnInstanceCreate` / `PICO_OnSessionCreate` with fabricated handles;
- use `Pxr_SetUserDefinedSettings()` as bootstrap;
- manually call `newTrackerClient::setShareMemoryFD()` / `resetShareMemory()`;
- change tracker pairing/tracking mode;
- perform reset/calibration from the reader;
- use runtime deviceId as persistent identity;
- bake the measured A->B transform into the runtime reader;
- bake body/device mount correction into the raw runtime adapter.

The `GetSwiftBattery` full-serial path is known to abort `pvrtrackingservice` through an uncaught `std::invalid_argument` and is intentionally absent from this repository.

## Build

Host-side decoder/clock tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Android runtime target: arm64-v8a / API 29 or later. Build with the Android NDK CMake toolchain; the Android target links `dl` and `log` and includes the AArch64 singleton-call shim.

## C ABI quick start

```c
pico_ot_config_t cfg;
pico_ot_default_config(&cfg);

// Research only until the tested libtrackingclient GNU Build ID is pinned:
cfg.allow_unverified_runtime = 1;

pico_ot_backend_t* backend = pico_ot_create();
if (pico_ot_start(backend, &cfg) == PICO_OT_OK) {
    size_t n = pico_ot_device_count(backend);
    for (size_t i = 0; i < n; ++i) {
        pico_ot_device_info_t dev;
        pico_ot_get_device(backend, i, &dev);

        pico_ot_pose_t pose;
        pico_ot_poll_pose(backend, dev.serial, &pose);
    }
}
pico_ot_destroy(backend);
```

## Intended next layer

A separate wrapper/bridge should consume this backend and decide how to expose:

- 6DoF position+orientation,
- IMU/orientation-only data during optical loss,
- simultaneous 6DoF + IMU logical streams,
- SlimeVR input,
- direct SteamVR generic tracker output.

Those policies intentionally do not belong in this backend.
