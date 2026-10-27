# RR-00B runtime diagnostic provider

The Windows PC receiver service accepts `--diagnostic-jsonl PATH`. Without this
option no diagnostic worker, output file, or receiver callback is installed.
`--version --json` prints embedded build provenance and exits before opening
the service mutex, status mapping, or network ports. This change does not deploy
or launch the service. Enabling diagnostics on an already running executable
requires a separately authorized later deployment/start.

The side channel is UTF-8 JSONL with HIL schema version 1. MonakaProtocol,
the imported kits, POTB packet layouts, private runtime ABI, and tracking policy
are unchanged. The provider runs on the PC; the Android acquisition app is not
instrumented in this phase.

## Observation boundaries

- `hardware_serial` comes from the POTB full serial and is stable for the physical
  device. `backend_device_id` comes from runtimeDeviceId and has sender-session
  scope; these domains must not be compared interchangeably.
- Body roles are not assigned by this backend. `role` is null and tracker events
  carry `role_unavailable_reason`. Body assignment remains owned by MonakaVR.
- Pose events represent newly admitted receiver sequences. The callback runs
  after each decoded packet, so intermediate validity transitions are visible
  even when multiple packets are processed in one latest-only receiver Pump.
- Position validity combines the actual POTB position flag, finite position,
  and receiver freshness. Orientation validity reports finite quaternion
  sample-sanity evidence (norm 0.5 to 1.5), not a vendor-authoritative optical or
  rotation-only claim. Diagnostic component flags remain independent; production
  observation output still fails closed to modality none unless both are valid.
  Event monotonic/UTC timestamps are captured on the producer at observation.
  The writer adds diagnostic_delivery_age_ns in the same source clock domain;
  pose details include sample_receive_monotonic_ns. HIL can preserve source
  sample age plus queue delay without subtracting clocks from different processes.
- First pose and first valid position reset for each observed sender token change
  or source-observation reconnect. Cached poses from before the reconnect boundary
  cannot count as first poses. A 1-second absence of accepted pose/device state
  is `source_observation_timeout`, explicitly not an authoritative transport
  disconnect. Pose freshness expires at the existing receiver timeout independently.
  The service samples the idle diagnostic clock after Receiver Pump. A caller's
  clock older than the latest accepted receipt is an unusable observation:
  Poll leaves diagnostic state unchanged instead of reporting timeout or
  synthesizing freshness. A subsequent usable clock still detects actual
  expiration, reconnect and sender-session boundaries.
- Tracker registration is receiver-cache membership. Enumeration snapshots do
  not prove live physical presence or a complete inventory, so `inventory_complete`
  remains false. Old cache registration is retired on session/source boundaries.
  Backend device ID changes emit tracker_updated.
- Graceful termination emits component_stopping, tracker_unregistered,
  session_disconnected, component_stopped, and diagnostic_summary, drains the worker,
  then closes the file. Abrupt process death needs HIL process/stream observation;
  no provider can manufacture its own post-death event.

## Output and backpressure

The service thread is the single producer of a bounded 1024-slot SPSC ring.
128 slots are reserved for critical lifecycle, registration, validity, first
sample, and fatal transitions. The writer thread serializes and flushes lines;
the tracking thread performs no file I/O or queue mutex wait. Ordinary pose
overflow increments dropped_events. Critical overflow also increments
critical_dropped_events and permanently sets observation_complete=false.
Successful events and the terminal summary expose counters and explicit gaps.
Sink failure also sets completeness false. A permanently unwritable stream must
be treated as missing evidence by HIL. The shutdown drain can wait on the writer;
this occurs after tracking stops.

## Build provenance

CMake generates provenance on each build, including full Git SHA, branch/ref,
dirty flag, version, and UTC timestamp. The header is linked into the PC service
and exposed by diagnostics and `--version --json`. binary_sha256 is null in the
embedded object because an executable cannot embed its own final hash. The
post-build `.build-provenance.json` companion records the actual executable SHA256
with the identical metadata. `pico_diagnostic_build_mapping` verifies both mappings.
Keep the executable and companion together for HIL collection.

## Verification

MSVC 19.51 / Visual Studio 18 2026, Release, Windows x64. The original nine CTest
entries pass with synthetic loopback permissions; diagnostic lifecycle/backpressure
and exact build mapping tests are added. The lifecycle fixture covers same-token
source reconnect, token changes, first sample resets, hardware serial stability,
runtime identity, independent validity changes, intra-Pump transitions, stale pose,
fatal/unknown event preservation, timestamps, and terminal summary. The blocked
writer test proves reserved fatal capacity and fail-closed critical overflow;
the failing writer test proves sink failure handling.

The deferred-loop-clock regression sends 32 real synthetic UDP packets through
Publisher and Receiver, then repeats an earlier pre-Pump clock after each packet
callback. It checks no false session/registration/first-pose edges and unchanged
receiver samples, exact 500ms pose and 1s session boundaries, a real source timeout,
same-token reconnect, and zero diagnostic drops. This is offline software
evidence; physical transport reliability remains unverified by this test.

An initial synthetic peak-load sample (not hardware validation) measured:

| Mode | iterations | loop time | CPU time | working set | written | dropped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OFF idle | 50000 | 1.207 ms | 2 ms | 6.97 MB | 0 | 0 |
| ON idle | 50000 | 1.315 ms | 2 ms | 7.03 MB | 5 | 0 |
| OFF active | 5000 | 29.427 ms | 30 ms | 7.04 MB | 0 | 0 |
| ON active | 5000 | 202.721 ms | 203 ms | 7.07 MB | 5015 | 0 |

ON active file-write latency averaged 7.832 microseconds (max 143.8 microseconds),
on the separate writer; approximately 24738 events/second. Synthetic producer
overhead was approximately 34.7 microseconds/sample. These unpaced saturation
measurements characterize this host only; real 5-tracker timing and CPU impact
are NOT RUN. No real hardware, physical/software restart, SteamVR/VD operation,
or live production process launch was performed.
