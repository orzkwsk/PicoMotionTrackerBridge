# SteamVR coordinate validation

Date: 2026-09-12
Branch: `feature/steamvr-output`

This note records hardware validation performed after HMD->PC transport and SteamVR Generic Tracker registration were proven.

## Orientation

The PICO OutputA quaternion is preserved by the Backend and converted only in the SteamVR output layer.

Hardware validation established the current output candidate:

```text
OpenVR x = -PICO z
OpenVR y =  PICO y
OpenVR z = -PICO x
OpenVR w =  PICO w
```

The orientation diagnostic zero uses a world-axis-relative delta:

```text
q_relative = q_now * inverse(q_zero)
```

This ordering was validated with the tracker reset from multiple initial physical orientations. Pitch, roll and yaw kept the same OpenVR axis meaning after zeroing, so the diagnostic reference no longer rotates the reported axes with the tracker's orientation at reset time.

Hardware result:

```text
pitch axis/direction : PASS
roll axis/direction  : PASS
yaw axis/direction   : PASS
zero from rotated initial poses : PASS
```

The orientation-zero feature remains diagnostic. It is not a body-mount calibration and does not belong in the Backend.

## Position

Initial SteamVR bring-up used the PICO OutputA position directly in meters.

Observed before position zeroing:

```text
movement axis directions : correct candidate
movement scale           : correct candidate
absolute origin          : different from SteamVR playspace
```

A diagnostic per-tracker Position Zero was then added:

```text
p_relative = p_now - p_zero
```

Hardware validation confirmed that after removing the absolute origin offset, tracker translation preserved direction and distance over approximately room-scale movement.

Hardware result:

```text
relative X/Y/Z direction : PASS
relative translation scale : PASS
position-zero capture/clear : PASS
translation-only world-alignment hypothesis : still viable
```

This supports continuing with the rigid alignment model:

```text
p_openvr = R * p_pico + t
```

with `R = I` as the current first hypothesis. A common SteamVR-world translation `t` still needs to be measured against a SteamVR-native reference pose.

## Next calibration gate

Do not turn the per-tracker Position Zero diagnostic into production calibration. It intentionally gives every tracker its own origin and therefore cannot define one shared playspace transform.

The next calibration step should measure a common translation against a SteamVR-native reference, preferably by physically co-locating one PICO Motion Tracker with a SteamVR controller and solving:

```text
t = p_reference_openvr - p_pico
```

Then apply the same `t` to every PICO tracker and validate at several room positions. If residual error changes with position, re-open the `R = I` assumption and solve the full rigid transform.

## Scope

No Backend, private ABI, UDP protocol, clock mapping or tracker identity semantics are changed by these coordinate diagnostics. Coordinate conversion and calibration remain SteamVR/output-layer responsibilities.
