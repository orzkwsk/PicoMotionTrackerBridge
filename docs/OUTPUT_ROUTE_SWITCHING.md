# Output routing through PC Bridge Service

The PC Bridge Service is the only Windows-side consumer of the HMD UDP stream.
SteamVR, MonakaVR, and Utility no longer compete for the same socket.

## Ports

```text
PICO HMD publisher
    -> PC Bridge Service UDP 29765

PC Bridge Service
    -> SteamVR local fan-out UDP 29766
    -> MonakaVR local fan-out UDP 29767
    -> Utility local fan-out UDP 29768
```

`29765` is reserved for HMD -> PC ingress. Output consumers bind only their dedicated loopback fan-out ports.

## Output modes

The Bridge Service supports four user-facing output policies:

```text
SteamVR
MonakaVR
Both
Disabled
```

These modes control only SteamVR and MonakaVR forwarding.

The Utility fan-out on `29768` remains active in every mode, including `Disabled`, so detailed monitoring and advanced operations can stay available without exposing that information in the Bridge GUI.

`Both` sends the same normalized latest-state stream to SteamVR and MonakaVR simultaneously. `Disabled` keeps HMD ingress, the service cache, status IPC, and Utility feed alive while suppressing game/runtime outputs.

The SteamVR driver stays loaded and keeps its local receiver on `29766`. If SteamVR output is disabled, its last tracker states naturally become stale and are submitted as disconnected/invalid. Existing Generic Tracker device objects remain registered until `vrserver` restarts, as required by OpenVR's server-driver lifetime model.

The Monaka receiver C API defaults to local UDP `29767`. A future Utility receiver should explicitly bind `29768`.

## Persistence and runtime control

The requested output mode is persisted in:

```text
%LOCALAPPDATA%\PicoMotionTrackerBridge\output_route.txt
```

Current values:

```text
steamvr
monaka
both
disabled
```

Legacy values `steamvr-direct` and `monaka-external` are still accepted during migration.

The tray signals:

```text
Local\PicoOTBridge_OutputRouteUpdated
```

The PC Bridge Service owns that event and reloads the requested route. It also polls the persisted route periodically so a missed event does not leave the service permanently out of sync.

## Applied status

The service publishes a small local shared-memory status block:

```text
Local\PicoOTBridge_ServiceStatus
```

It contains the applied output route, current tracker count, active HMD sender session, and a monotonic update timestamp. The tray uses this only for a compact applied/requested indication; detailed tracker diagnostics remain the responsibility of Utility.

## GUI boundary

The Bridge GUI remains intentionally small. It is for:

- output selection;
- world translation/alignment;
- calibration;
- orientation/position zero diagnostics.

Detailed per-tracker state, raw diagnostics, battery interpretation, pose age, packet sequencing, and advanced backend operations belong in Utility rather than the Bridge GUI.

## Historical startup

The procedure below records the retired one-click path. Current startup is
`../launchers/Start_PicoMotionTrackerBridge.cmd`, which opens the prebuilt control
GUI and receiver-owning tray from `Build/runtime`. It accepts only `--check`, and
never builds, registers a driver, or controls the HMD. Build and deployment remain
explicit developer commands.

The removed `Start_All.cmd` performed runtime preparation before starting the backend:

1. verify/build the current SteamVR driver when necessary;
2. register the driver for first-run use;
3. verify/build and start the PC Bridge Service;
4. start the PICO Backend app;
5. start the tray/WPF control UI.

If the SteamVR driver binary needs replacement while `vrserver.exe` is running, startup stops with an explicit request to exit SteamVR instead of attempting to overwrite a loaded DLL.
