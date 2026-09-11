# Known limitations

1. **Private ABI**: `getBodyTrackerData` offset `0x47dc8` and the 392-byte result layout are firmware-build dependent.
2. **Build ID baseline not yet pinned in the handoff**: strict mode requires the caller to provide the expected `libtrackingclient.pxr.so` GNU Build ID. Research mode can explicitly allow an unverified runtime.
3. **TrackerInfo allocator ownership unresolved**: one bounded enumeration allocation is intentionally leaked. Repeated refresh is disabled by default.
4. **Battery bucket semantics provisional**: TrackerInfo `+43` is exposed only as a raw optional value. No percentage is claimed.
5. **Linear acceleration semantics provisional**: `+248` is exposed after SI scaling but documented as a candidate filter/predictor acceleration field.
6. **Angular velocity frame provisional**: values are preserved as rad/s and marked device/local-frame candidate; no world-frame transform is applied.
7. **Orientation validity**: no universal vendor orientation-valid bit has been proven. The backend only reports whether a sane quaternion sample is present.
8. **No mount/body transform**: raw PICO frame conversion and attachment mount correction are intentionally not hard-coded.
9. **Connection refresh**: the current safe default performs one TrackerInfo enumeration. Accurate long-running disconnect state requires the allocator problem to be solved or an alternative callback/Binder path to be implemented.
10. **No output adapter**: SteamVR/SlimeVR routing belongs in the subsequent bridge repository.
