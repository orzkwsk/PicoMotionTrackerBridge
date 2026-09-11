# Runtime validation

This document records observations from the standalone non-XR backend harness on real PICO hardware. It is evidence for the backend contract, not a claim about undocumented vendor semantics beyond what was observed.

## Validation baseline

Target:

- PICO 4 Ultra
- Android arm64-v8a
- normal non-XR APK/process
- `libtrackingclient.pxr.so` / `TrackingClient`
- `getBodyTrackerData` offset `0x47dc8`
- `algo_result_tracker` size `392`

Validated runtime identity:

```text
GNU Build ID: d1b55bc0598ffff93cc01eaf089793f8
```

Strict runtime verification was enabled with:

```text
allowUnverified=0
verified=1
```

The private function address resolved as runtime base + `0x47dc8`. No OpenXR instance/session was created by the harness.

## Device identity and enumeration

Persistent identity is the full tracker serial number. Runtime device IDs are session/runtime metadata only.

A five-tracker run enumerated:

```text
PC2310MLK7031247G  runtimeDeviceId=1
PC2310MLK7031254G  runtimeDeviceId=2
PC2310MLK8040979G  runtimeDeviceId=3
PC2310MLK7200684G  runtimeDeviceId=4
PC2310MLK7200128G  runtimeDeviceId=5
```

Existing runtime IDs remained stable while trackers were added during the staged 2 -> 3 -> 4 -> 5 validation sequence.

The one-time `TrackerInfo` enumeration reports:

```text
intentionallyLeakedEnumerationBytes=360
```

This is consistent with a five-entry allocation at 72 bytes per `TrackerInfo`; it is allocation capacity, not the number of connected trackers.

### Dynamic discovery behavior

The backend device registry is startup-enumerated.

Observed behavior:

```text
known tracker power-cycle
    -> ReinitializeRuntime()
    -> recovery possible without re-enumeration

tracker absent at Backend start
    -> tracker powered on later
    -> ReinitializeRuntime()
    -> new serial is NOT added to the Backend registry

same tracker configuration
    -> Backend Stop/Start
    -> TrackerInfo re-enumeration
    -> newly powered tracker appears
```

Therefore the validated initial policy is:

```text
known-device recovery  -> ReinitializeRuntime()
topology/device change -> Backend restart / re-enumeration
```

Repeated enumeration remains disabled by default because allocator ownership is unresolved.

## Validity model

Connection snapshot, positional validity, orientation sample presence, and vendor raw state are separate states.

Observed combinations include:

```text
connectedSnapshot=1
positionValid=0
orientationSamplePresent=1
```

During optical loss:

- orientation continues to update;
- angular velocity continues to update;
- position may freeze;
- linear velocity commonly becomes zero.

During reacquisition the numeric position field can change while `positionValid == false`.

Consumer rule:

```text
if positionValid == false:
    do not consume position
```

The raw tracking/state byte at vendor offset `+65` has been observed as `0`, `1`, `2`, `3`, and `4`. It does not map cleanly to positional validity and remains diagnostic-only.

No universal vendor orientation-valid bit has been proven. The backend reports `orientationSamplePresent` for a sane quaternion sample rather than claiming `orientationValid`.

## Timestamp semantics

The backend preserves:

- vendor output timestamp from offset `+0` as `sourceTimestamp`;
- local `CLOCK_MONOTONIC` observation/query timestamp as `observedMonotonicNs`;
- dynamically mapped source -> local monotonic timestamp.

`mappedMonotonicValid` remained true throughout the stress/lifecycle runs.

`CurrentFirmwareRuntimeAdapter::ReadTrackerPose()` passes the local monotonic timestamp as the timestamp argument to `getBodyTrackerData()`. Across the 120 Hz stress run, the returned vendor timestamp tracked that query timestamp almost exactly, with a run-specific fixed clock-domain offset.

Therefore distinct source timestamps at 60/120 Hz prove stable service of distinct query times, not a native 60/120 Hz sensor publication rate.

## Query-rate stress

### 60 Hz

Two connected trackers were queried at a nominal 60 Hz during rapid motion, occlusion, and reacquisition.

Each one-second window reported:

```text
polls=60
unique=60
repeats=0
```

No vendor-call error, decoder failure, or native crash was observed.

### 120 Hz-class, two trackers

A subsequent two-tracker run used a nominal 120 Hz query rate.

Each tracker produced 23 complete windows:

```text
23 windows x 120 polls = 2760 polls/tracker
unique timestamps        = 2760
repeated timestamps      = 0
```

Observed effective cadence was approximately 116-118 Hz per tracker. The harness is not a hard real-time scheduler; the result establishes 120 Hz-class query stability, not sensor update rate.

### 120 Hz-class, five trackers

The final five-tracker run held all five serials in one Backend instance for approximately 41 seconds.

Across all five trackers, 195 one-second cadence windows were observed. Every captured window reported:

```text
polls=120
unique=120
repeats=0
```

Effective cadence was generally approximately 116-117 Hz per tracker after startup, with no timestamp repetition, vendor-call failure, decoder failure, or native crash attributable to five-tracker operation.

All five trackers achieved valid 6DoF in the same run, and every tracker independently demonstrated motion and optical loss/reacquisition.

This establishes five-tracker high-rate Backend stability on the validated runtime.

## ReinitializeRuntime while tracking

`ReinitializeRuntime()` was invoked while a tracker had valid 6DoF. The call completed with:

```text
REINITIALIZE status=ok trackerMap=rebuilt clockMapper=reset
```

The tracker remained position-valid across the call, with sub-millimeter adjacent logged position delta, negligible orientation delta, no velocity spike, no polling failure, and immediate valid timestamp mapping.

## Known-tracker power-cycle recovery

A previously registered tracker was powered off and entered a stale-output condition:

- `positionValid=0`;
- quaternion/angular velocity became byte-for-byte constant;
- `connectedSnapshot` remained `1`;
- poll calls still succeeded;
- timestamps continued to advance.

This proves that startup connection state, successful polling, and advancing timestamps are not sufficient proof of a physically live tracker.

After the tracker was powered back on, `ReinitializeRuntime()` rebuilt the tracker map and the same serial/runtime ID resumed changing data, then reacquired optical position.

Known-tracker recovery through `ReinitializeRuntime()` is therefore validated.

## HMD sleep/resume lifecycle

The HMD was put to sleep after 6DoF acquisition while the non-XR harness remained running.

During sleep:

- the process and poll thread stayed alive;
- position became invalid;
- orientation continued;
- approximately 116-118 Hz query service continued;
- every cadence window remained `120/120/0`;
- mapped timestamps remained valid;
- no crash or native error was observed.

After HMD resume, optical position naturally reacquired without calling `ReinitializeRuntime()`.

A later deliberate post-resume `ReinitializeRuntime()` also completed successfully and preserved timestamp mapping/recovery behavior.

Therefore both natural HMD sleep/resume recovery and post-resume reinitialization are validated.

## Reference frame / recenter observations

The tracker was moved, held stationary, the PICO HMD was recentered, and the visible playspace displacement was then compensated through OVR/SteamVR playspace movement.

Observed behavior:

- PICO Output A raw tracker pose did not exhibit the corresponding HMD/playspace transform;
- OVR playspace movement did not feed back into the raw PICO pose;
- recenter could coincide with temporary `positionValid=false` / optical reacquisition;
- after reacquisition, the tracker returned in the same raw reference frame rather than a recentered raw origin.

For the tested stack, the useful model is therefore:

```text
PICO Output A raw frame
    -> higher-level HMD/recenter transform
    -> SteamVR/playspace transform
```

The backend should not apply HMD recenter or OVR playspace compensation to `PicoOutputA`. Coordinate conversion and mount/body transforms remain output/integration-layer responsibilities.

## Confirmed backend behavior

As of the completed standalone validation:

```text
Android arm64 runtime loading          PASS
Private ABI guarded call               PASS
Strict Build ID verification           PASS
Tracker enumeration                    PASS
Full-serial identity                   PASS
Position decoding                      PASS
Quaternion decoding                    PASS
Linear velocity decoding               PASS
Angular velocity decoding              PASS
Dynamic timestamp mapping              PASS
Position-validity detection            PASS
Orientation during optical loss        PASS
Optical reacquisition                  PASS
Two-tracker simultaneous 6DoF          PASS
60 Hz query stress                     PASS
120 Hz-class query stress              PASS
Reinitialize during valid 6DoF         PASS
Known-tracker power-cycle recovery     PASS via ReinitializeRuntime()
HMD sleep/resume natural recovery      PASS
Post-resume ReinitializeRuntime        PASS
Startup-only device discovery model    CHARACTERIZED
Backend restart topology refresh       PASS
PICO recenter/raw-frame behavior       CHARACTERIZED
OVR playspace/raw-frame isolation      PASS
Five-tracker enumeration               PASS
Five-tracker simultaneous 6DoF         PASS
Five-tracker 120 Hz-class query stress PASS
Five-tracker loss/reacquisition        PASS
```

## Standalone backend closure

The planned standalone PICO Backend hardware validation is complete for the currently pinned runtime profile.

The remaining items are known limitations or higher-layer work, not blockers for closing the Backend validation phase:

- private ABI remains firmware-build specific;
- `TrackerInfo` allocator ownership is unresolved;
- dynamic live connection/discovery still needs a future no-leak strategy if required;
- battery bucket, acceleration semantics, and angular-velocity frame remain provisional;
- no universal orientation-valid bit has been identified;
- PICO -> SteamVR/MonakaVR basis conversion and mount/body transforms are intentionally outside the raw backend;
- SteamVR output is not implemented in this branch.

The Backend phase therefore stops here. The next implementation phase must begin in a separate bridge/output branch after review of the backend contract.
