# SteamVR output phase

This document defines the first implementation boundary above the completed PICO Backend.

## Branch boundary

Base:

```text
feature/pico-ot-backend
  e2737c209fc3739c83db8ca2f676d3e4bda87a5e
```

Output work:

```text
feature/steamvr-output
```

The validated Backend remains unchanged. Any PICO private-ABI change must be justified independently from SteamVR/output needs.

## Runtime topology

The validated PICO Backend executes inside a normal non-XR Android process on the HMD. A SteamVR server driver executes inside `vrserver.exe` on the Windows PC. They cannot be linked into one process.

The bridge therefore has an explicit transport boundary:

```text
PICO Motion Tracker
    ↓
PICO TrackingClient / SHM
    ↓
Validated Android Backend
    ↓ TrackerState
HMD bridge sender
    ↓ UDP, versioned packet protocol
PC bridge receiver
    ↓ latest state keyed by full serial
SteamVR server driver
    ↓ TrackedDeviceClass_GenericTracker
SteamVR
```

The transport is above the Backend contract. The Backend does not know about sockets, SteamVR, body roles, calibration, or PC clock domains.

## Why UDP for the pose path

Pose transport is latest-value data:

- old pose samples should not queue behind new samples;
- packet loss is preferable to head-of-line blocking;
- five trackers at approximately 120 queries/s each remain a small UDP data rate;
- topology/device state can be repeated periodically rather than relying on a single reliable event.

Protocol v1 therefore uses UDP datagrams. Discovery/pairing may be added separately and must not change pose semantics.

## Wire protocol v1

The shared `PicoOT::BridgeCore` target owns the codec.

Header:

```text
magic             4 bytes   "POTB"
version           2 bytes   1
message type      2 bytes
sender session    8 bytes
sequence          4 bytes
payload size      4 bytes
```

All integer and IEEE-754 binary64 fields are explicitly encoded little-endian; raw C++ struct layout is never sent on the wire.

Messages currently defined:

```text
POSE
DEVICE_STATE
TIME_SYNC_REQUEST
TIME_SYNC_RESPONSE
```

The sender session ID changes when the HMD bridge sender restarts. This prevents a PC receiver from interpreting a restarted sequence counter or old device topology as the same sender lifetime.

### POSE

Carries:

- full tracker serial
- runtime device ID as diagnostic/session metadata
- startup connection snapshot
- `positionValid`
- `orientationSamplePresent`
- raw state byte `+65`
- PICO source timestamp
- HMD observed monotonic timestamp
- HMD mapped monotonic timestamp
- position in meters
- quaternion XYZW
- linear velocity in m/s
- angular velocity in rad/s
- reference-frame enum value
- angular-velocity-frame enum value

Acceleration is intentionally omitted from the first SteamVR transport contract because its vendor semantics remain provisional and SteamVR output does not require it for initial bring-up.

### DEVICE_STATE

Carries low-rate management information:

- serial
- runtime device ID
- startup connection snapshot
- charging flag
- raw optional battery bucket
- HMD observation time

The current Backend does not claim an authoritative live connection bit. The PC must use stream liveness separately from the startup snapshot.

## Clock domains

Three time concepts must remain separate:

```text
PICO/vendor timestamp
    ↓ Backend mapping
HMD CLOCK_MONOTONIC
    ↓ bridge time sync
PC monotonic clock
```

The HMD-to-PC step uses NTP-style four-timestamp exchange:

```text
t0 PC send
 t1 HMD receive
 t2 HMD send
 t3 PC receive
```

Estimated HMD-minus-PC offset:

```text
((t1 - t0) + (t2 - t3)) / 2
```

The short rolling estimator selects the observation with the lowest network RTT to reduce queueing-jitter contamination.

A fixed HMD/PC timestamp offset is prohibited.

## SteamVR driver target

The first driver target will use OpenVR SDK 2.15.6 and expose each serial as:

```text
TrackedDeviceClass_GenericTracker
```

Roles remain user-managed through SteamVR's Manage Trackers UI; the driver must not hard-code waist/foot/knee roles.

The driver receive path must not block `IServerTrackedDeviceProvider::RunFrame()`.

Bring-up order:

1. driver DLL/manifest loads in SteamVR;
2. UDP receiver accepts/decodes bridge packets;
3. full serial creates/reuses a Generic Tracker device;
4. latest pose is submitted with `TrackedDevicePoseUpdated`;
5. stale stream marks the device disconnected/invalid;
6. HMD-to-PC clock sync drives pose age / `poseTimeOffset`;
7. coordinate alignment is added explicitly above raw `PicoOutputA`.

## Coordinate policy

No guessed PICO-to-SteamVR basis is baked into the Backend or protocol.

Validated facts:

- Backend pose is `PicoOutputA`;
- PICO HMD recenter does not rewrite that raw frame;
- OVR/SteamVR playspace motion does not feed back into that raw frame.

Therefore an explicit rigid transform/calibration step is required above the receiver before the output can be considered spatially correct in SteamVR.

Initial driver bring-up may use identity driver-space transform only to verify transport/device registration. That state is diagnostic, not final calibration.

Angular velocity remains a device/local-frame candidate. It must not be blindly submitted as SteamVR world/driver-space angular velocity until its frame conversion is proven.

## Current implementation status

Implemented on `feature/steamvr-output`:

- versioned packet codec;
- sender-session identification;
- pose/device-state packet models;
- NTP-style HMD-to-PC clock estimator;
- cross-platform UDP socket abstraction;
- host-side codec/clock/UDP loopback tests in CMake.

Next implementation unit:

```text
Android Backend -> UDP publisher
Windows UDP receiver -> SteamVR server-driver skeleton
```
