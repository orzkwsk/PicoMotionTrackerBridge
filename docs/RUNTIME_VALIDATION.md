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

Strict runtime verification was then enabled with that Build ID. The device harness started with:

```text
allowUnverified=0
verified=1
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
    -> strict Build ID verification
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

Trackers subsequently transitioned to:

```text
positionValid=1
```

with changing position and non-zero linear velocity. Simultaneous two-tracker 6DoF operation was observed.

### Optical loss and reacquisition

Complete transitions were observed repeatedly:

```text
positionValid=1
    -> positionValid=0
    -> positionValid=1
```

Orientation samples continued during invalid-position intervals.

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

It does not map cleanly to positional validity. In particular, samples were repeatedly observed with:

```text
positionValid=1, state65=0
```

Invalid-position samples were also observed with state values `0`, `2`, and `4`.

Therefore `rawTrackingState65` remains diagnostic-only. Output modules must not use it as the sole tracking-validity switch.

## Timestamp and 60 Hz source-cadence validation

The backend preserves:

- raw source timestamp;
- local `CLOCK_MONOTONIC` observation timestamp;
- dynamically mapped source->local monotonic timestamp.

`mappedMonotonicValid` remained true during the logged stress run. No fixed source/local offset is used.

A subsequent harness run polled each connected tracker at a target rate of 60 Hz while both trackers were deliberately moved rapidly, mixed in the cameras' view, repeatedly occluded, and reacquired.

The run lasted approximately 26 seconds and produced 26 one-second cadence windows per tracker, 52 windows total. Every cadence window reported:

```text
polls=60
unique=60
repeats=0
```

Observed source-rate summary:

```text
PC2310MLK8040979G
  estSourceHz range: 58.92 .. 59.53 Hz
  mean:              ~59.29 Hz
  largest delta:     24.029792 ms

PC2310MLK7200684G
  estSourceHz range: 58.92 .. 59.53 Hz
  mean:              ~59.29 Hz
  largest delta:     23.789401 ms
```

The short min/max delta excursions are scheduling/call-time jitter around an otherwise consistent ~16.87 ms mean interval. Crucially, there were no repeated source timestamps at a 60 Hz polling rate, including during optical loss and rapid motion.

This establishes that the exposed source stream updates at least fast enough to provide a distinct sample for every ~60 Hz poll under the tested conditions. It does **not** yet prove that the native source ceiling is exactly 60 Hz; a 120 Hz polling test is required to distinguish a ~60 Hz source ceiling from a higher-rate source.

No `PicoOTTest` poll/start error, native crash, or decoder sanity failure was observed during the stress run. Logged quaternion samples remained normalized while high angular velocities and repeated optical loss/reacquisition were exercised.

## Confirmed backend behavior

As of this validation:

```text
Android arm64 runtime loading       PASS
Private ABI guarded call            PASS
Strict Build ID verification        PASS
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
60 Hz distinct-source sampling      PASS
60 Hz motion/occlusion stress       PASS
```

## Next runtime tests

1. Poll at 120 Hz to identify the source update ceiling and repeated-sample pattern.
2. Exercise HMD sleep/resume followed by `ReinitializeRuntime()`.
3. Exercise tracker disconnect/reconnect over a long-lived process.
4. Repeat while Virtual Desktop owns the XR session.
5. Only after the standalone backend remains stable, add the SteamVR output module.
