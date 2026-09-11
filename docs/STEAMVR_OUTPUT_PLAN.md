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

The sender session ID changes when the HMD bridge sender starts. This prevents a PC receiver from interpreting a restarted sequence counter or old device topology as the same sender lifetime.

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

The current Backend does not claim an authoritative live connection bit. The PC uses stream liveness separately from the startup snapshot.

## Clock domains

Three time concepts remain separate:

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

## PC receiver policy

The Windows receiver is non-blocking and drains all currently queued datagrams on each pump. It retains only the newest accepted state per full serial.

Default liveness policy:

```text
pose packet age <= 500 ms -> stream live
pose packet age >  500 ms -> stale/disconnected
```

`connectedSnapshot` remains metadata from Backend enumeration and is not used as authoritative live connection state.

When the HMD sender session ID changes, the PC receiver clears cached topology and clock-sync state before accepting the new sender lifetime.

## SteamVR driver target

The first driver target uses OpenVR SDK 2.15.6 and exposes each serial as:

```text
TrackedDeviceClass_GenericTracker
```

Roles remain user-managed through SteamVR's Manage Trackers UI; the driver does not hard-code waist/foot/knee roles.

The driver receive path does not block `IServerTrackedDeviceProvider::RunFrame()`.

Current bring-up behavior:

1. driver DLL/manifest loads in SteamVR;
2. UDP receiver accepts/decodes bridge packets;
3. full serial creates/reuses a Generic Tracker device;
4. latest pose is submitted with `TrackedDevicePoseUpdated`;
5. stale stream marks the device disconnected/invalid;
6. HMD-to-PC clock sync drives `poseTimeOffset` when available;
7. coordinate alignment remains deliberately unresolved until transport/device registration is validated.

## Coordinate policy

No guessed PICO-to-SteamVR basis is baked into the Backend or protocol.

Validated facts:

- Backend pose is `PicoOutputA`;
- PICO HMD recenter does not rewrite that raw frame;
- OVR/SteamVR playspace motion does not feed back into that raw frame.

Therefore an explicit rigid transform/calibration step is required above the receiver before the output can be considered spatially correct in SteamVR.

Initial driver bring-up uses an identity driver-space transform only to verify transport/device registration. That state is diagnostic, not final calibration.

Linear and angular velocity are intentionally submitted as zero during this bring-up stage. The global PICO->OpenVR basis is not yet calibrated, and angular velocity remains a device/local-frame candidate.

## Current implementation status

Implemented on `feature/steamvr-output`:

- versioned packet codec;
- sender-session identification;
- pose/device-state packet models;
- NTP-style HMD-to-PC clock estimator;
- cross-platform monotonic clock abstraction;
- cross-platform non-blocking UDP socket abstraction;
- Android/HMD Backend publisher;
- Windows latest-state receiver;
- stream-stale handling;
- Generic Tracker SteamVR provider/device skeleton;
- OpenVR SDK 2.15.6 optional build integration;
- SteamVR driver manifest/package layout;
- BackendTestApp manual PC IPv4 / bridge start-stop controls;
- standalone PC receiver CLI;
- protocol, clock, UDP, publisher, and receiver host tests;
- host/SteamVR build and driver registration helper scripts.

## Next validation gate

Do not calibrate coordinates yet.

Validate in this order:

```text
1. Host build + CTest
2. PC receiver CLI listening on UDP 29765
3. BackendTestApp -> PC bridge start
4. Confirm serial/pose stream and HMD<->PC clock sync in CLI
5. Build/register SteamVR driver
6. Confirm same serials appear as Generic Trackers
7. Only then derive PicoOutputA -> OpenVR alignment
```

The first hardware gate is transport only. SteamVR and coordinate calibration are intentionally excluded until HMD->PC pose delivery is proven.