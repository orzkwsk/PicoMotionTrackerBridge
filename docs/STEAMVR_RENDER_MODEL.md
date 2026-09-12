# SteamVR PICO Tracker Render Model

## Purpose

The SteamVR output driver can expose a PICO Motion Tracker-like render model instead of relying on SteamVR's generic tracker visualization.

This feature is output-layer only. It does not change Backend pose decoding, transport, calibration, validity, or tracker identity.

## Model layout

The build generates two driver-local render models:

```text
build-steamvr/steamvr/pico_ot_bridge/
  rendermodels/
    pico_motion_tracker/
      pico_motion_tracker.obj
      pico_motion_tracker.mtl
      pico_motion_tracker.png
    pico_motion_tracker_hidden/
      pico_motion_tracker_hidden.obj
      pico_motion_tracker_hidden.mtl
      pico_motion_tracker_hidden.png
```

The visible model is an approximate PICO Motion Tracker core model with a rounded white body, button/LED indication, USB-C opening, and front marking.

Local model axes follow the SteamVR/OpenVR-facing operational convention used during tracker validation:

```text
-Z = front / logo face
+Y = button + LED / up
-X = USB-C / left
```

The hidden model is intentionally microscopic. The driver switches to it rather than clearing `Prop_RenderModelName_String`, because clearing the property may cause SteamVR to fall back to a generic tracker model.

## Build

Stop SteamVR, then run:

```powershell
.\scripts\build_steamvr_driver.ps1
```

A successful build must report both visible and hidden OBJ paths. If either file is missing, the build script fails.

## Runtime controls

Start SteamVR with the driver, then use:

```powershell
.\scripts\steamvr_render_model.ps1 -Action Show
.\scripts\steamvr_render_model.ps1 -Action Hide
```

The current visibility state applies to all PICO trackers. Newly registered PICO trackers inherit the provider's current state.

The state is session-local and returns to visible when the SteamVR driver restarts.

## Diagnostics

Run:

```powershell
.\scripts\steamvr_render_model.ps1 -Action Check
```

This verifies the generated asset files and prints recent `vrserver.txt` lines related to PICO OT render-model selection.

Expected driver log lines include:

```text
pico_ot_bridge: render-model controls ready (Show/Hide named events)
pico_ot_bridge: render model shown for <serial> -> {pico_ot_bridge}/rendermodels/pico_motion_tracker
pico_ot_bridge: render model hidden for <serial> -> {pico_ot_bridge}/rendermodels/pico_motion_tracker_hidden
```

## Current validation status

Code path and package layout are implemented, but real SteamVR render-model loading and live Show/Hide refresh still require Windows/SteamVR hardware validation.

If SteamVR does not refresh the model immediately after a Show/Hide command, first verify that the property-change log appears. A runtime reload/reconnect mechanism can then be added separately without changing the model assets or Backend.
