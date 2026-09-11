# BackendTestApp

Minimal non-XR Android harness for validating `PicoMotionTrackerBackend` on PICO 4 Ultra.

It deliberately does not own an OpenXR session. On startup it:

1. starts the backend with `allow_unverified_runtime=1` for initial validation;
2. logs the loaded `libtrackingclient.pxr.so` GNU Build ID and resolved pose function address;
3. performs the backend's one allowed TrackerInfo enumeration;
4. logs serial/deviceId/connection/charging/battery raw bucket;
5. polls connected trackers at 10 Hz and logs pose/validity/timestamps.

## Build

From the repository root:

```powershell
.\BackendTestApp\build_test_app.ps1
```

The script uses the Unity `2022.3.22f1` Android SDK/NDK/OpenJDK/Gradle by default.

## Build, install, start and log

Connect the PICO by ADB/USB and run:

```powershell
.\BackendTestApp\deploy_test_app.ps1
```

To install/start without holding the terminal in logcat:

```powershell
.\BackendTestApp\deploy_test_app.ps1 -NoLogcat
```

Later:

```powershell
.\BackendTestApp\run_logcat.ps1
```

Expected tag: `PicoOTTest`.

## First validation

Record the `RUNTIME buildId=...` line first. The harness intentionally permits the unverified runtime only so the actual tested Build ID can be obtained. After the recovered offset is confirmed on-device, pin that Build ID in the harness/backend configuration and disable the override.

Normal optical tracking should show `conn=1 posValid=1 ori=1`. Hide a tracker from HMD cameras: expected behavior is `conn=1 posValid=0 ori=1`, frozen position, near-zero linear velocity, and continuing quaternion/angular velocity updates. Restore visibility and verify `posValid` returns to 1.

Do not enable repeated tracker enumeration for long-running tests; the current vendor vector deallocation path is intentionally unresolved.
