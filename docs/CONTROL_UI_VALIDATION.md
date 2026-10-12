# Control UI software receipt — 2026-10-13 JST

Baseline: `refactor/monaka-layer-separation` at
`8718f81d31e1650741eb7dd2c575f97b06888cca`. Work: `codex/ui-ux-hardening`.

Windows x64 / Visual Studio 18 2026 / MSVC 19.51 / SDK 10.0.26100.0,
.NET 8.0.424 and SDK 10.0.401, Python 3.13.15.

| Check | Actual result |
| --- | --- |
| Baseline Release native | PASS 9 distinct tests; blocked sandbox UDP cases passed host rerun |
| `scripts/steamvr_control_gui.ps1 -BuildOnly -Configuration Debug` | PASS PC receiver, service/core and WPF; no live services/devices started |
| Same launcher, Release | PASS |
| Full Debug CTest (`build/ui-baseline`) | PASS 9/9 |
| Full final Release CTest (`build-bridge-host`, all targets built) | PASS 9/9 |
| Control.Tests Debug and Release | PASS real current-user local pipe/framing/reconnect, fixed native status ABI/version/count/seqlock/heartbeat, atomic persistence/failure, desired alignment reapply with fake driver, five-device UI states, busy/duplicate/cancel/timeout/disconnect/invalid/shutdown |
| Control.Smoke Release | PASS offscreen five-device WPF layout/binding; screenshot under `build/ui-smoke` |
| GUI/service build warnings | 0 warnings / 0 errors in final builds |
| `git diff --check` | PASS |
| Physical/interactive checks | HIL NOT RUN / interactive GUI NOT RUN |

Receipts: `build/control-debug-tests.log`, `build/control-launcher-native-tests.log`,
`build/ui-baseline/control-debug-results.xml` and
`build-bridge-host/control-release-results.xml`.
The first CTest attempt in the launcher directory omitted tests because the launcher
builds only its runtime target; that partial receipt was retained. All test targets
were then built and the full final 9/9 suite passed. Initial WPF binding/selection
problems found by offscreen smoke were repaired and rerendered.

Validation uses fake clients/drivers, isolated test status mappings and temporary
config. It never starts the real HMD backend, sends vendor commands or runs actual
SteamVR calibration. Runtime provider, protocol, ABI, prediction, transforms and
legacy fan-out were not redesigned. The new read-only control status is optional;
failures cannot disable tracking. Historical extraction remains hash-pinned to its
old source and was not regenerated. Source-bound release packaging/deployment was
not run. Merge into the selected lineage is software-reviewable; physical PICO,
SteamVR reapply/calibration/output and device liveness remain hardware gates.
