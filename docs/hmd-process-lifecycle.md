# HMD process ownership and receiver status

The native Backend, poll thread and UDP publisher belong to the HMD application
process. An Activity attaches to the process-owned saved-intent controller once.
Reopening/recreating the screen does not stop or restore either native component.
The controller factory is static and retains application preferences, never an
Activity. Destroyed screens remove only their own status-refresh callbacks.

Normal **Start Backend** preserves an existing backend and poll thread. Explicit
**Restart Backend / re-enumerate trackers** stops and joins the old thread before
one fresh enumeration; it does not replace the independently running publisher.
This is also the explicit recovery action after startup detects zero trackers.
No periodic enumeration or unverified private ABI access has been introduced.

**Start UDP Bridge** preserves a running sender session at the same endpoint.
Changing the endpoint still replaces the sender. **Restart UDP Bridge** explicitly
stops and starts it once. **Stop UDP Bridge** disables saved startup intent;
**Stop Backend** stops polling independently. Failed sender starts remain stopped
when the screen is reopened; manual Start retries. A new application process may
restore the last successful saved endpoint once. Android process termination is
not prevented: this change adds neither a foreground service nor a wake lock.

The read-only native snapshot separates backend/publisher state, enumerated
count, lifetime poll/send counters, and ages of successful poll/send calls.
Successful polling is not proof of physical connectivity or valid tracking.
Successful UDP sends do not acknowledge PC receipt, which remains UNKNOWN here.
The screen's last command result is labeled separately from native actual state.

`scripts/start_pico_backend.ps1` normally opens the screen without force-stopping
the process. `-ForceRestart` retains the explicit process-replacement operation.
The script and on-device instrumentation must not be executed during offline work.

The PC title is **OT Tracker Receiver**. Its compatibility output menu explicitly
states that Disabled affects legacy output while the receiver keeps running;
Exit stops the owned receiver through the existing lifecycle protocol. The v1
shared-status heartbeat is accepted only within three seconds in the Windows QPC
clock. Cached tracker count does not prove pose freshness. A missing or stale
status is labeled unavailable/stale, not a confirmed process shutdown.

The compatibility GUI still contains legacy SteamVR controls. Final Native Agent
and Win32-tray consolidation, and removal of old writers, remain gated by the
Bridge responsibility transfer. Fixed production IPC names prevent an isolated
full GUI boot on a host with an active receiver. BuildOnly and pure display tests
do not establish visual/interactive OS acceptance.

## Offline regression

`pico_hmd_lifecycle` compiles the actual JNI orchestration source with Android/JNI
boundary shims and a fake physical backend. The real poll loop, thread ownership,
publisher and ephemeral loopback socket remain in use. It checks duplicate Start,
zero-device explicit restart, sender-session continuity, observations, failed
start, eight concurrent Starts and final resource cleanup. An optional
`PICO_HMD_BASELINE_SOURCE` creates a separately invoked expected-failing executable
for the old source; it is not registered as a passing CTest.

The original ten plain-JVM tests concern saved intent and publisher dispatch, not
Activity ownership. They remain, with one explicit-restart case added. The Android
smoke oracle now requires sender-session continuity across recreation; compiling
it does not mean it has run. Static-loss, sleep/wake, real Activity recreation and
five-tracker physical recovery require a separately authorized HMD run.
