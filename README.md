# PicoMotionTrackerBridge

Native PICO Motion Tracker bridge project derived from the `Pico4Ultra_ResearchTools` native-tracker investigation.

The repository is intentionally layered:

- the **backend core** owns PICO-specific runtime access, decoding, device identity, validity semantics, unit normalization, lifecycle handling, and timestamp mapping;
- **bridge/output modules** consume that normalized state and expose it to systems such as SteamVR or MonakaVR.

The `feature/pico-ot-backend` branch contains the standalone Backend implementation and real-device validation harness. The Backend validation phase is complete for the currently pinned PICO runtime profile. SteamVR output is the next phase and is intentionally not implemented in this branch.

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
        ├─ SteamVR generic tracker        [next phase]
        ├─ MonakaVR input adapter         [planned]
        └─ other consumers / IPC          [future]
```

The backend runtime path does not create an OpenXR instance/session.

## Backend contract

- Persistent identity: full tracker serial number.
- Runtime device ID: session/runtime metadata only; never used as persistent identity.
- Output A pose decoding:
  - source timestamp `+0x000`
  - position `+0x008` (mm -> m)
  - quaternion `+0x020` (XYZW)
  - position validity `+64`
  - raw vendor quality/state `+65`
  - linear velocity `+0x0E0` (mm/s -> m/s)
  - linear acceleration candidate `+0x0F8` (mm/s^2 -> m/s^2, semantically provisional)
  - angular velocity `+0x110` (rad/s; device/local-frame candidate)
- TrackerInfo decoding:
  - connection/state `+0`
  - runtime deviceId `+4`
  - full serial `+18..35`
  - charging `+42`
  - raw battery bucket candidate `+43`
- Source timestamp and local monotonic observation timestamp are preserved separately.
- Source -> local timestamp mapping is dynamically rebased; no fixed timestamp offset is used.
- Known-device lifecycle recovery entry point: `ReinitializeRuntime()` / `pico_ot_reinitialize_runtime()`.
- C++ API and stable C ABI are both exposed.

## Important validity semantics

`getBodyTrackerData() == 0` is **not** a connection or positional-validity test.

Validated semantics:

```text
connectedSnapshot        <- TrackerInfo value captured at Backend enumeration
positionValid            <- algo_result_tracker byte +64
orientationSamplePresent <- sane quaternion sample can continue during optical loss
raw state +65            <- diagnostics only; not a validity flag
```

During reacquisition the vendor position field can change while `positionValid == 0`. Consumers must ignore position whenever `positionValid` is false.

The raw byte at `+65` has been observed as `0`, `1`, `2`, `3`, and `4`, including `state65=0` while `positionValid=1`. It is preserved for diagnostics only.

The backend deliberately names the orientation flag `orientationSamplePresent`, not `orientationValid`, because a universal vendor orientation-valid flag has not been recovered.

## Runtime compatibility guard

`getBodyTrackerData()` was recovered at `libtrackingclient.pxr.so + 0x47dc8` for the currently validated runtime. This is not a public ABI.

Validated GNU Build ID:

```text
d1b55bc0598ffff93cc01eaf089793f8
```

Normal use should pin this Build ID through `expectedLibraryBuildIdHex` / `expected_library_build_id_hex` and keep `allowUnverifiedRuntime=false`.

Do not silently carry the offset to a new PICO firmware build. A different Build ID is an unknown ABI until explicitly validated.

## Tracker enumeration allocator limitation

`getSwiftTrackerConnectState(std::vector<TrackerInfo>&)` allocates the vector buffer inside vendor/system libc++. Cross-runtime allocator ownership has not been proven safe.

The backend therefore preserves this safety rule:

- initial enumeration is allowed once per Backend start;
- repeated in-instance enumeration is disabled by default;
- the bounded leaked allocation is reported in `RuntimeDiagnostics::intentionallyLeakedEnumerationBytes`;
- `allowRepeatedLeakyEnumeration` exists only as an explicit investigation escape hatch.

On the validated runtime one enumeration reports `360` intentionally leaked bytes, consistent with a five-entry `TrackerInfo` allocation at 72 bytes per entry.

Validated topology behavior:

```text
known tracker power-cycle
    -> ReinitializeRuntime()
    -> recovery

tracker absent at Backend start
    -> power on later
    -> ReinitializeRuntime()
    -> not added

Backend Stop/Start
    -> fresh enumeration
    -> newly powered tracker discovered
```

The initial production-safe policy is therefore to use `ReinitializeRuntime()` for known-device recovery and restart the Backend when tracker topology changes.

## Coordinate/reference behavior

The Backend exposes `ReferenceFrame::PicoOutputA` without HMD/SteamVR compensation.

Hardware validation showed that PICO HMD recenter and OVR/SteamVR playspace movement do not apply the corresponding transform to the raw Output A tracker pose. Recenter may temporarily invalidate optical position and trigger normal reacquisition, but the raw reference frame remains stable after recovery.

PICO -> SteamVR/MonakaVR basis conversion, quaternion continuity policy, and physical mount/body correction therefore belong above the Backend.

## Mandatory prohibited paths

The backend core does not and must not:

- call `GetSwiftBattery(fullSerial, ...)`;
- create/fake an OpenXR session;
- call `PICO_OnInstanceCreate` / `PICO_OnSessionCreate` with fabricated handles;
- use `Pxr_SetUserDefinedSettings()` as bootstrap;
- manually call private SHM FD/reset functions;
- change tracker pairing/tracking mode;
- perform reset/calibration from the raw reader;
- use runtime deviceId as persistent identity;
- bake the measured A -> B transform into the runtime reader;
- bake body/device mount correction into the raw runtime adapter.

The `GetSwiftBattery` full-serial path is known to abort `pvrtrackingservice` through an uncaught `std::invalid_argument` and is intentionally absent.

## Validation status

Standalone Backend validation completed on PICO 4 Ultra with the pinned runtime profile:

- Windows/MSVC host build;
- decoder/clock CTest;
- Android arm64-v8a NDK build;
- APK execution through `libtrackingclient.pxr.so`;
- strict Build ID verification;
- full-serial identity and enumeration;
- optical 6DoF acquisition/loss/reacquisition;
- IMU orientation/angular-velocity continuation while position is invalid;
- simultaneous two-tracker 6DoF;
- 60 Hz query stress;
- 120 Hz-class two-tracker query stress;
- reinitialize during valid 6DoF;
- known-tracker power-cycle recovery;
- HMD sleep/resume natural recovery;
- post-resume reinitialize;
- startup-only discovery behavior characterization;
- Backend restart topology refresh;
- PICO recenter/raw-frame characterization;
- OVR playspace/raw-frame isolation;
- five-tracker enumeration;
- simultaneous five-tracker 6DoF;
- five-tracker 120 Hz-class query stress;
- five-tracker loss/reacquisition.

In the final five-tracker run, all five trackers were queried concurrently for approximately 41 seconds. Captured cadence windows consistently reported `polls=120`, `unique=120`, `repeats=0`, generally at approximately 116-117 effective queries/second/tracker after startup.

This establishes API/query stability at 120 Hz-class. It does **not** establish the physical tracker sensor/optical native update rate because the returned source timestamp tracks the requested query time.

See:

- `docs/RUNTIME_VALIDATION.md` for observed runtime evidence and final PASS matrix;
- `docs/KNOWN_LIMITATIONS.md` for remaining constraints;
- `docs/ARCHITECTURE.md` for the frozen Backend/output boundary.

## Build

Host-side decoder/clock tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Android runtime target: arm64-v8a / API 29 or later. Build with the Android NDK CMake toolchain; the Android target links `dl` and `log` and includes the AArch64 singleton-call shim.

The repository also contains `BackendTestApp`, a normal non-XR Android validation harness used to validate the Backend independently from output modules.

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
        // orientation_sample_present is independent of position_valid.
    }
}
pico_ot_destroy(backend);
```

## Bridge layering

The Backend phase is closed on `feature/pico-ot-backend`. The next implementation phase should be a separate bridge/output branch.

```text
backend/
  PICO runtime + normalized tracker state       [validated]

outputs/
  steamvr/                                      [next]
  ...

integrations/
  monaka/                                       [future]
```

Output-specific policy must remain outside the firmware-sensitive PICO runtime adapter unless a newly discovered PICO-runtime requirement genuinely belongs below the normalized state boundary.
