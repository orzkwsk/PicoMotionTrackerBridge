# Known limitations

1. **Private ABI**: `getBodyTrackerData` offset `0x47dc8` and the 392-byte result layout are firmware-build dependent.
2. **Single validated runtime profile**: the currently validated `libtrackingclient.pxr.so` GNU Build ID is `d1b55bc0598ffff93cc01eaf089793f8`. A different Build ID must be treated as an unknown ABI until explicitly validated.
3. **TrackerInfo allocator ownership unresolved**: one bounded enumeration allocation is intentionally leaked. Repeated refresh is disabled by default.
4. **Battery bucket semantics provisional**: TrackerInfo `+43` is exposed only as a raw optional value. No percentage is claimed.
5. **Linear acceleration semantics provisional**: `+248` is exposed after SI scaling but documented as a candidate filter/predictor acceleration field.
6. **Angular velocity frame provisional**: values are preserved as rad/s and marked device/local-frame candidate; no world-frame transform is applied.
7. **Orientation validity**: no universal vendor orientation-valid bit has been proven. The backend only reports whether a sane quaternion sample is present.
8. **Invalid position values are not guaranteed frozen**: during optical loss position often freezes and linear velocity becomes zero, but during reacquisition the numeric position field can change while `positionValid == false`. Consumers must ignore position whenever `positionValid` is false.
9. **Raw tracking state `+65` is diagnostic only**: values `0`, `1`, `2`, and `4` have been observed and do not map cleanly to positional validity. `state65=0` has occurred while `positionValid=1`.
10. **No mount/body transform**: raw PICO frame conversion and attachment mount correction are intentionally not hard-coded in the runtime reader.
11. **Connection refresh**: the current safe default performs one TrackerInfo enumeration. Accurate long-running disconnect state requires the allocator problem to be solved or an alternative callback/Binder path to be implemented.
12. **High-rate cadence not yet characterized**: 10 Hz polling has been validated. 60/120 Hz polling and the actual source sample-update rate still need measurement.
13. **Sleep/resume not yet validated in the bridge harness**: `ReinitializeRuntime()` exists, but HMD sleep/resume recovery has not yet been exercised in this repository.
14. **Virtual Desktop coexistence not yet regression-tested with this harness**: the backend remains non-XR by design, but coexistence should be explicitly verified after the high-rate runtime test.
15. **SteamVR output not implemented yet**: this repository is now the bridge project, but the current feature branch contains only the backend core and validation harness. SteamVR output should be added as a separate module above the backend contract.
