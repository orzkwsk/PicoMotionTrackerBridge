# Runtime validation

This document records observations from the standalone non-XR backend harness on real PICO hardware. It is evidence for the backend contract, not a claim about undocumented vendor semantics beyond what was observed.

## 2026-09-11 validation baseline

Target:

- PICO 4 Ultra
- Android arm64-v8a
- normal non-XR APK/process
- `libtrackingclient.pxr.so` / `TrackingClient`
- `getBodyTrackerData` offset `0x47dc8`
- `algo_result_tracker` size `392`

Loaded runtime identity:

```text
GNU Build ID: d1b55bc0598ffff93cc01eaf089793f8
```

Initial enumeration returned two connected trackers:

```text
PC2310MLK7200684G  runtimeDeviceId=4
PC2310MLK8040979G  runtimeDeviceId=3
```

Persistent identity remains the full serial number. The runtime device ID is treated as session/runtime metadata only.

The one-time TrackerInfo enumeration reported:

```text
intentionallyLeakedEnumerationBytes=360
```

This is consistent with a five-entry allocation at 72 bytes per `TrackerInfo`; it is not a count of connected trackers.

## Startup and runtime access

Observed successful sequence:

```text
System.loadLibrary(test JNI)
    -> pico_ot_create()
    -> pico_ot_start()
    -> runtime diagnostics
    -> TrackerInfo enumeration
    -> continuous pico_ot_poll_pose()
```

`pico_ot_start()` returned success and the recovered private function address resolved as runtime base + `0x47dc8`.

No OpenXR instance/session was created by the harness.

## Component validity observations

### Initial IMU-only / no optical position

Both connected trackers were observed with:

```text
connectedSnapshot=1
positionValid=0
orientationSamplePresent=1
```

During this condition:

- orientation continued to update;
- angular velocity continued to update;
- position was often frozen;
- linear velocity was commonly zero.

This confirms that connection, positional validity, and orientation sample availability are separate states.

### 6DoF acquisition

A tracker subsequently transitioned to:

```text
positionValid=1
```

with changing position and non-zero linear velocity. The second tracker later acquired valid position as well, demonstrating simultaneous two-tracker 6DoF operation.

### Optical loss and reacquisition

A complete transition was observed repeatedly:

```text
positionValid=1
    -> positionValid=0
    -> positionValid=1
```

Orientation samples continued during the invalid-position interval.

Importantly, the numeric position field is **not guaranteed to remain frozen while invalid**. During reacquisition it was observed to change before `positionValid` returned to true.

Consumer rule:

```text
if positionValid == false:
    do not consume position
```

Do not infer positional validity from whether the position value is frozen, moving, plausible, or near the previous sample.

## Raw vendor byte +65

The raw tracking/state byte at offset `+65` was observed with values including:

```text
0
1
2
4
```

It does not map cleanly to positional validity. In particular, samples were observed with:

```text
positionValid=1, state65=0
```

Therefore `rawTrackingState65` remains diagnostic-only. Output modules must not use it as the sole tracking-validity switch.

## Timestamp observations

At the initial 10 Hz harness rate, source timestamps advanced by approximately 100 ms per poll, as expected for a caller sampling at 10 Hz.

The backend preserved:

- raw source timestamp;
- local `CLOCK_MONOTONIC` observation timestamp;
- dynamically mapped source->local monotonic timestamp.

`mappedMonotonicValid` remained true during the test. No fixed source/local offset is used.

This 10 Hz test does **not** characterize the vendor's native sample-update rate. A higher polling rate is required to distinguish caller cadence from source cadence.

## Confirmed backend behavior

As of this validation:

```text
Android arm64 runtime loading       PASS
Private ABI guarded call            PASS
Tracker enumeration                 PASS
Full-serial identity                PASS
Position decoding                   PASS
Quaternion decoding                 PASS
Linear velocity decoding            PASS
Angular velocity decoding           PASS
Dynamic timestamp mapping           PASS
Position-validity detection         PASS
Orientation during optical loss     PASS
Optical reacquisition               PASS
Two-tracker simultaneous 6DoF       PASS
```

## Next runtime tests

1. Run strict Build ID verification rather than the investigation override.
2. Poll at 60 Hz and record the number of distinct source timestamps per second.
3. If stable, repeat at 120 Hz to identify the source update ceiling/repeated samples.
4. Exercise HMD sleep/resume followed by `ReinitializeRuntime()`.
5. Exercise tracker disconnect/reconnect over a long-lived process.
6. Repeat while Virtual Desktop owns the XR session.
7. Only after the standalone backend remains stable, add the SteamVR output module.
