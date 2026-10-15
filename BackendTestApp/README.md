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
The lifecycle fix is based on `dev@534df851` and uses `versionCode=5`
(`versionName=0.1.0-dev.5`), allowing an ordinary in-place update of earlier debug
APKs, including the interim main-based versionCodes 3 and 4.

## UDP startup and saved settings

The backend starts first on each Activity creation. The screen always shows
`Backend: RUNNING / STOPPED / ERROR` and `UDP: RUNNING / STOPPED / ERROR`
separately, together with the UDP endpoint. Stopping/restarting the backend does
not change the independent publisher's state or saved intent. UDP RUNNING means
the local publisher started; it does not guarantee remote packet delivery.

Enter a numeric IPv4 literal and press `Start UDP Bridge -> PC:29765`.
The existing `Use Wi-Fi subnet (/24)` button fills the first three octets from
the HMD's active Wi-Fi IPv4, keeping a valid PC host octet already entered. This
is a `/24` input helper; enter the PC's last octet yourself. Older APKs' saved
`pc_ipv4` is retained as an input hint until the first successful START using the
new settings. Editing input or pressing the subnet helper does not overwrite the
last successful endpoint or change automatic restore intent.
Hostnames and IPv6 are unsupported. The shared native transport parser validates
the address before the publisher opens a socket or reports START success.
Broadcast literals pass syntax validation; this does not add broadcast delivery support.

Only after START succeeds, SharedPreferences `bridge_settings` stores
`bridge_pc_ipv4` and `bridge_enabled=true`. On the next Activity/app launch, the
saved IP is restored into the input and enabled intent triggers one automatic
START after the backend start result has been displayed. Each publisher restart
creates a new sender session. A fresh installation leaves UDP STOPPED.

`Stop UDP Bridge` stops the publisher and saves `bridge_enabled=false`, so the
next launch leaves UDP stopped while retaining the last successful IP. Activity
destruction stops both components without changing the saved intent.

A failed manual START never overwrites the last successful IP or enabled intent.
A failed automatic START caused by an invalid saved IP disables automatic
restore; other failures retain the existing intent for the next launch, without
retrying in the current Activity. An invalid restart stops the previous publisher
and shows UDP ERROR. Failure reasons and restore/START/STOP events use logcat tag
`PicoOTTest`.

Run the app's dependency-free JVM lifecycle regression tests with:

```powershell
.\BackendTestApp\test_lifecycle.ps1
```

These tests cover fresh start, successful settings persistence, recreation,
explicit stop, invalid input, endpoint changes, repeated START, and transient
failures. They complement the host transport/publisher tests; they do not replace
on-device validation. On a PICO, check manual START and PC packet arrival, relaunch
without pressing START and check a new sender session, then check that explicit
STOP survives a relaunch and invalid IPv4 fails immediately. Issue #6 remains
open for separate evidence; Issue #7 tracking/lost investigation is deferred.

The optional on-device instrumentation exercises the real buttons, UI status,
SharedPreferences, Activity recreation, and independent backend/publisher STOP.
It resets only the two bridge settings at the beginning and leaves UDP enabled
toward the supplied PC IPv4 at the end. It restarts the test app; run it when the
app is available for this smoke. It does not assert optical validity or packet
arrival, which must be checked at the PC receiver separately.

```powershell
.\BackendTestApp\build_test_app.ps1 -BuildSmokeTests
$taskAdb = 'C:\Program Files\Unity\Hub\Editor\2022.3.22f1\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\platform-tools\adb.exe'
# Update the main app first using the regular debug APK, then install the test APK:
& $taskAdb -s <device> install -r -t BackendTestApp/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
& $taskAdb -s <device> shell am instrument -w -e pc_ipv4 <PC-IPv4> com.orz.picootbackendtest.test/com.orz.picootbackendtest.BridgeSmokeInstrumentation
```

Success prints `PASS: lifecycle smoke`. Add `-r` to `am instrument` when raw
instrumentation status, including the success code `INSTRUMENTATION_CODE: -1`, is needed.
For a separate process-restart check, force-stop and launch the main app again,
without pressing START, and compare the new native `BRIDGE started ... session=`
log with the sender session received at the PC. No PC packet is expected without
tracker/device samples; backend RUNNING and publisher RUNNING alone do not prove
packet delivery.

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
