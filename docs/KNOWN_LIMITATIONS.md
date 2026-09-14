# Known limitations

This document lists limitations that remain after the standalone PICO Backend validation phase was completed on the pinned runtime profile.

1. **Private ABI**: `getBodyTrackerData` offset `0x47dc8` and the 392-byte result layout are firmware-build dependent.
2. **Single validated runtime profile**: the currently validated `libtrackingclient.pxr.so` GNU Build ID is `d1b55bc0598ffff93cc01eaf089793f8`. A different Build ID must be treated as an unknown ABI until explicitly validated.
3. **TrackerInfo allocator ownership unresolved**: one bounded enumeration allocation is intentionally leaked. Repeated refresh is disabled by default.
4. **Topology refresh requires Backend restart by default**: a tracker absent when the Backend starts is not added by `ReinitializeRuntime()`. A Backend Stop/Start performs a fresh enumeration and discovers it. Known registered trackers can recover from a power cycle through `ReinitializeRuntime()`.
5. **Live connection state is not authoritative**: `DeviceInfo.connected` is the startup enumeration snapshot. A tracker can physically disappear while the cached value remains `1`.
6. **Successful polling is not a liveness proof**: a disconnected/stale tracker may still return successful calls with advancing query timestamps and a frozen payload. Higher layers must not equate poll success with physical connectivity.
7. **Battery bucket semantics provisional**: TrackerInfo `+43` is exposed only as a raw optional value. No percentage is claimed.
8. **Linear acceleration semantics provisional**: `+248` is exposed after SI scaling but documented as a candidate filter/predictor acceleration field.
9. **Angular velocity frame provisional**: values are preserved as rad/s and treated as device/local-frame candidate; no world-frame transform is applied.
10. **Orientation validity**: no universal vendor orientation-valid bit has been proven. The backend only reports whether a sane quaternion sample is present.
11. **Invalid position values are not guaranteed frozen**: during optical loss position often freezes and linear velocity becomes zero, but during reacquisition the numeric position field can change while `positionValid == false`. Consumers must ignore position whenever `positionValid` is false.
12. **Raw tracking state `+65` is diagnostic only**: values `0`, `1`, `2`, `3`, and `4` have been observed and do not map cleanly to positional validity. `state65=0` has occurred while `positionValid=1`.
13. **No mount/body transform**: raw PICO frame conversion and attachment mount correction are intentionally not hard-coded in the runtime reader.
14. **No PICO -> SteamVR/MonakaVR basis transform in the Backend**: `TrackerState` remains in `ReferenceFrame::PicoOutputA`. Consumer/output layers own coordinate conversion.
15. **Recenter is not applied by the Backend**: tested PICO HMD recenter and OVR playspace movement did not transform the raw Output A pose. Recenter may coincide with temporary optical invalidity/reacquisition, which is handled through the normal validity model.
16. **120 Hz-class means query stability, not sensor rate**: two- and five-tracker tests successfully serviced distinct query timestamps at approximately 116-118 Hz per tracker, but the returned source timestamp strongly tracks the requested query time and does not establish native sensor/optical publication frequency.
17. **Five trackers are validated only on the pinned runtime/profile**: enumeration, simultaneous 6DoF, motion, loss/reacquisition, and 120 Hz-class query stress passed with five trackers. This does not guarantee equivalent behavior on a different firmware/runtime Build ID.
18. **Virtual Desktop coexistence is architectural, not an XR integration guarantee**: the Backend itself remains a normal non-XR process and does not create an OpenXR session. Output-layer SteamVR integration is a separate validation phase.
19. **Historical milestone, not current feature absence**: "SteamVR output is not implemented yet" described the standalone Backend feature branch. The reviewed `refactor/pc-bridge-service-fanout` base already contains SteamVR driver/calibration/GUI and Monaka C receiver compatibility. Task 2 Phase A retains them while adding the common Observation provider. Removing those old responsibilities is gated on Task 4/5 handoffs in Phase B.
20. **Common Observation remains a candidate**: the Task 1 kit is verified and pinned, but master reconciliation and physical tracker validation of this new PC path remain pending. Pose receipt time preserves only PC-side admission age, not HMD sensor age. Metadata presence and battery fraction remain unknown; cached charging is also unknown without a live-validity signal.
21. **Space changes require recalibration**: every PC provider restart and accepted POTB sender-session change increases the persisted C1 space revision. Old transforms must not be reused silently. Stop the service before editing its identity file; copying it to another backend installation would duplicate its source identity.
22. **Retired sessions fail closed**: the single HMD ingress owner remembers up to 1024 retired sessions per process lifetime. Once full, it refuses further session switches until restarted, rather than resurrecting old sessions. Phase A provider tracks at most 256 devices. These bounds do not solve the pinned runtime's discovery/liveness limitations.

## Explicitly prohibited runtime-reader paths

The Backend must not:

- call `GetSwiftBattery(fullSerial, ...)`;
- create or fake an OpenXR session;
- fabricate `PICO_OnInstanceCreate` / `PICO_OnSessionCreate` handles;
- use `Pxr_SetUserDefinedSettings()` as bootstrap;
- manually open/reset tracker SHM through private FD setters;
- change pairing/tracking mode;
- perform reset/calibration from the raw reader;
- use runtime device ID as persistent identity;
- hard-code the measured Output A/B transform into the generic Backend;
- bake body/device mount correction into the runtime adapter.

`GetSwiftBattery(fullSerial, ...)` is specifically excluded because that path was observed to abort `pvrtrackingservice` through an uncaught `std::invalid_argument`.
