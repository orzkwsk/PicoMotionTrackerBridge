# PICO control frontend and service

Run `Start_GUI.cmd`, or `scripts/steamvr_control_gui.ps1`, on Windows with the
.NET 8 desktop SDK and the existing Visual Studio C++/CMake toolchain. The launcher
builds the PC receiver and control components, starts the headless control service
hidden, and opens the existing WPF adjustment/calibration layout. Single-instance
guards prevent duplicate windows/services. It does not start the HMD backend,
change pairing or stop an existing backend when the GUI closes.

## Normal operation

- The header separates control-service, PC receiver, SteamVR driver, requested
  output and applied output state. Refresh retries status after an error.
- **Start PC receiver** and **Stop PC receiver** are explicit native service
  lifecycle operations. Stop uses the receiver's graceful local stop event;
  there is no process kill. Start/stop waits for bounded status confirmation.
- Up to five serials appear with connection snapshot, tracking/validity and receive
  age. A connection snapshot is not a live vendor heartbeat guarantee. Missing
  data is Unknown / Not available. Stale poses are invalidated; battery remains
  Unavailable because no validated battery path was added.
- Existing X/Y/Z, 1 mm / Shift 10 mm nudges, Read, Apply, Reset, Save, Load + Apply,
  tracker/reference selection and Measure + Apply remain available. Selection is
  preserved by serial during polling. Driver operations are unavailable until
  driver IPC is available. Alignment values are finite and range checked.
- Apply / measurement does not save implicitly. The unsaved indicator compares
  fields with the service's saved alignment. Save is atomic and the service owns
  both alignment and output-route files. Measurement runs asynchronously, has a
  bounded timeout and can be cancelled; cancellation terminates only the temporary
  measure-only calibrator and never the tracking service.
- Output route is in Advanced. Temporary zero controls are in Diagnostics /
  Research. Copy diagnostics and Open log folder are available from the normal
  window. Driver-internal zero does not become playspace/body assignment.

New HMD device discovery requires the existing HMD backend Stop/Start. Restarting
the PC receiver is not vendor re-enumeration. No private ABI, battery command,
HMD lifecycle, Fusion/IK, prediction or coordinate algorithm was changed.

## Control boundary

```text
Existing WPF view -> ControlViewModel -> PipeControlClient
  -> headless Control.Service -> existing driver IPC / calibrator / PC receiver
```

The GUI no longer opens memory mappings/events, invokes the calibrator or writes
alignment/output configuration. Its code-behind builds the existing view, binds
fields/commands, handles display interactions and awaits window close cleanup.
The headless owner is the normal driver-control IPC/persistence adapter. Existing
standalone legacy CLI/tray sources remain historical compatibility tools; do not
run them as simultaneous alignment/output writers with the control service.

Frontend JSON is version 1 with request IDs, bounded UTF-8 lines, duplicate-key and
version validation, explicit errors, current-user-only Windows Named Pipes, eight
bounded connections and partial-frame deadlines. The logical prefix is
`PicoOTBridge.Control.v1`; the endpoint appends `.Session.<Windows session ID>` so
another login session cannot route control to the wrong session-local driver.
No LAN/web listener is introduced. Service state/driver reapply polling is 1 Hz.

The PC receiver publishes a separate fixed five-device, read-only status mapping
at 4 Hz. It contains serials, boolean state, fixed receive age and a shared Windows
boot-clock heartbeat; no pose vectors, raw vendor data or fake battery/identity.
Readers validate version/count, seqlock generation and heartbeat before trusting
it. GUI polling stays 1 Hz. Only the native ingress owner reads Receiver state;
no worker or GUI enters the existing tracking loop. Status failure is optional
and does not disable the normal provider, legacy fan-out or SteamVR output.

The service reapplies its desired/saved alignment after driver restart. Malformed
saved configuration is preserved and reported. Paths remain
`%LOCALAPPDATA%/PicoMotionTrackerBridge/steamvr_alignment.json` and `output_route.txt`.
Logs are under that directory's `logs` folder. Diagnostics includes version/OS,
health, output, counts and errors, with the user-profile prefix redacted.

## Verification and limits

```powershell
./scripts/steamvr_control_gui.ps1 -BuildOnly -Configuration Release
dotnet build ui/Control.Tests/Control.Tests.csproj -c Release --disable-build-servers -p:UseSharedCompilation=false -m:1
dotnet ui/Control.Tests/bin/Release/net8.0-windows/Control.Tests.dll
dotnet build ui/Control.Smoke/Control.Smoke.csproj -c Release --disable-build-servers -p:UseSharedCompilation=false -m:1
dotnet ui/Control.Smoke/bin/Release/net8.0-windows/Control.Smoke.dll
```

Repeat with Debug and run existing native CTests in both configurations. UI tests
cover real local current-user pipe framing/reconnect with a fake owner, atomic
save/load and corruption/write failure, five-device state, busy/duplicate guard,
cancel/timeout/disconnect/invalid responses and shutdown. Offscreen smoke renders
WPF binding/layout with five mock devices under `build/ui-smoke`. Mocks are only
in separate test/smoke projects; the shipped GUI/service exposes no mock selector.

Interactive GUI operations and physical PICO/SteamVR calibration, five-tracker
enumeration, loss/recovery, output, restart/reapply and real device liveness are
**HIL NOT RUN**. New .NET 8 control applications require the .NET 8 desktop runtime.
The historical extraction packager intentionally exports its fixed legacy base;
it is not a package of this new frontend. No shared Bridge/Protocol contract or
cutover/legacy-path removal is claimed by software tests.
