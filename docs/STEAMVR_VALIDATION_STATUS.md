# SteamVR hardware validation status

This file records hardware-observed results for `feature/steamvr-output` separately from implementation hypotheses.

## Validated

- HMD -> PC UDP transport: PASS
- five PICO trackers visible as SteamVR Generic Trackers: PASS
- PICO position axis directions: PASS
- PICO position scale: PASS
- shared PICO -> SteamVR world translation: PASS for current room-scale validation; only small residual device/reference-point offset remains
- PICO relative orientation axes/directions: PASS
- orientation zero independent of tracker orientation at capture: PASS
- PICO linear velocity -> OpenVR `DriverPose_t::vecVelocity`: PASS
  - hardware observation: translational lag relative to the SteamVR controller disappeared
  - movement direction and amount remained matched to the controller
  - no observed overshoot or reverse-direction artifact in the validation motion

## Pending

- rotational prediction / OpenVR `vecAngularVelocity`
- exact PICO tracker physical pose-origin offset
- optional user-facing XYZ fine adjustment / persistent calibration UI
- more rigorous multi-point rigid alignment if a world-rotation residual is later observed

## Policy

The private PICO Backend remains unchanged by SteamVR calibration/prediction work. Coordinate alignment, prediction, mount offsets, and UI calibration belong above the normalized Backend contract.
