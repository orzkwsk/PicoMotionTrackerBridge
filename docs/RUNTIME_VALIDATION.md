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
3
4
```

It does not map cleanly to positional validity. In particular, samples were repeatedly observed with:

```text
positionValid=1, state65=0
```

Invalid-position samples were also observed with state values `0`, `2`, and `4`.

Therefore `rawTrackingState65` remains diagnostic-only. Output modules must not use it as the sole tracking-validity switch.

## Timestamp semantics and polling-rate validation

The backend preserves:

- vendor output timestamp from offset `+0` as `sourceTimestamp`;
- local `CLOCK_MONOTONIC` observation/query timestamp as `observedMonotonicNs`;
- dynamically mapped source->local monotonic timestamp.

`mappedMonotonicValid` remained true throughout the stress runs. No fixed source/local offset is assumed by the backend.

### Important timestamp observation

`CurrentFirmwareRuntimeAdapter::ReadTrackerPose()` passes the local `CLOCK_MONOTONIC` value (`observedNs`) as the timestamp argument to `getBodyTrackerData()`.

Across the 120 Hz stress log, the returned vendor `sourceTimestamp` tracked that query timestamp almost perfectly:

```text
sourceTimestamp - observedMonotonicNs
    ~= 9,106,271,118,823 ns
```

Across the logged pose samples the observed range of that offset was only about 53 ns, and the per-call source-timestamp delta was effectively identical to the local observation/query delta.

This strongly indicates that the `+0` field is the vendor-clock representation of the requested/query time (or a directly derived prediction timestamp), rather than an independent raw sensor acquisition timestamp.

Therefore:

- distinct `sourceTimestamp` values at 60/120 Hz prove that the private API successfully services distinct query times at those rates;
- they **do not prove** that the physical tracker sensors or optical solver natively publish new measurements at 60/120 Hz;
- the earlier term `source cadence` should not be interpreted as a native sensor frame rate.

The dynamic mapper remains useful because the vendor clock domain has a run-dependent offset from local `CLOCK_MONOTONIC`, and later sleep/resume testing still needs to verify continuity/rebasing behavior.

### 60 Hz query stress

The harness polled each connected tracker at a target rate of 60 Hz while both trackers were deliberately moved rapidly, mixed in the cameras' view, repeatedly occluded, and reacquired.

The run lasted approximately 26 seconds and produced 26 one-second windows per tracker. Every window reported:

```text
polls=60
unique=60
repeats=0
```

No `PicoOTTest` poll/start error, native crash, or decoder sanity failure was observed during the run.

### 120 Hz query stress

A subsequent run used a target query rate of 120 Hz with similar rapid motion, repeated occlusion/reacquisition, and a final stationary period.

For each tracker, 23 complete one-second windows were captured:

```text
23 windows x 120 polls = 2760 polls/tracker
unique timestamps        = 2760
repeated timestamps      = 0
```

Observed loop/query-rate summaries were:

```text
PC2310MLK8040979G
  estimated query cadence: 116.47 .. 118.26 Hz
  mean:                    ~117.12 Hz
  largest logged delta:    13.189193 ms

PC2310MLK7200684G
  estimated query cadence: 116.46 .. 118.06 Hz
  mean:                    ~117.11 Hz
  largest logged delta:    12.481745 ms
```

The achieved rate is slightly below the nominal 120 Hz because the validation loop sleeps for one nominal period after doing both tracker calls and logging/accounting work; this harness is not a hard real-time scheduler.

The important result is that the private API remained stable under approximately 117 queries/second/tracker with two trackers, rapid motion, optical loss/reacquisition, and no repeated query timestamps, vendor-call errors, decoder failures, or native crashes.

This establishes **120 Hz-class query stability**, not the tracker sensor/optical native update ceiling.

## Final stationary observations

During the final low-motion region (approximately loop 2600 onward in the 120 Hz run):

- `PC2310MLK7200684G` retained valid optical position for most of the interval; logged valid-position linear-speed magnitude averaged about 0.0058 m/s and peaked around 0.0126 m/s before position later became invalid near the end;
- its angular-speed magnitude averaged about 0.0020 rad/s in that low-motion region;
- `PC2310MLK8040979G` remained position-invalid during the same final region, with frozen/invalid position and zero linear velocity, while orientation continued and angular-speed magnitude also settled to about 0.0020 rad/s.

This is consistent with small residual tracking/estimation noise when stationary and with continued IMU orientation service while optical position is invalid.

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
60 Hz query stress                  PASS
120 Hz-class query stress           PASS
Final stationary behavior           PASS
```

## Next runtime tests

1. Exercise HMD sleep/resume followed by `ReinitializeRuntime()` and verify timestamp continuity/rebasing.
2. Exercise tracker disconnect/reconnect over a long-lived process.
3. Repeat while Virtual Desktop owns the XR session.
4. Only after the standalone backend remains stable, stop and review before starting the SteamVR bridge/output branch.
