# PICO reacquire admission (Phase 2C-4S)

Software-only readiness. `FINAL PICO REACQUIRE THRESHOLDS: NOT YET VALIDATED`.
`PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED`.

The PC PICO Observation provider owns this gate. It does not change the native
ABI, POTB, shared protocol schema, coordinate mapping, UDP lifecycle, or Core.
No production quality threshold has been selected. Default behavior is OFF.

## Configuration

`pico_ot_bridge_service --admission-config PATH --diagnostic-jsonl PATH` accepts
a JSON object with only these keys (all optional):

```json
{"enabled": false, "reacquire_confirm_ms": 0, "reacquire_min_samples": 0}
```

- `enabled`: boolean. Omitted means false. No environment or saved intent enables it.
- `reacquire_confirm_ms`: nonnegative int64 milliseconds, at most
  `9223372036854` (safe conversion to nanoseconds). Zero disables the duration condition.
- `reacquire_min_samples`: nonnegative int64. Zero disables the count condition.
- Both enabled conditions must pass. With both zero, the first valid progressing
  sample is admitted immediately. `{}` is explicit compatibility mode.
- Negative, fractional, out-of-range, incorrectly typed and unknown values fail
  before service startup. No silent fallback to a more permissive configuration.
- Offline fixture values 20 ms / 3 samples and 60 ms / 4 samples are **TEST-ONLY**.
  They are not defaults and must not be interpreted as PICO production guidance.

The effective configuration appears in the `component_ready` diagnostic event
under `details.pico_admission`, and at the start of replay output.

## State and time contract

`INVALID -> CANDIDATE -> ADMITTED`. Raw invalidity or failed numeric sanity
immediately returns either state to INVALID; there is no loss debounce.
Raw validity is the existing PICO full-pose predicate: native position flag,
orientation-sample flag, finite position and sane quaternion. Native flags are
also logged separately. Freshness is an independent existing admission check.
No PICO rotation-only authenticity is inferred from surviving numeric Q.

Duration uses immutable PC **receive monotonic time**, not wall clock or a
remapped device clock. The first valid progressing sample starts at elapsed 0,
count 1. Invalidity resets both fully. Count increments only for progressing
receive timestamps; time and count must both pass. Time does not advance by
polling an unchanged sample. After admission, count freezes; elapsed reports
the continuing valid interval from candidate start.

Duplicate receive timestamps neither begin nor advance confirmation. They
still invalidate immediately if canonical invalidity is presented. Regressing
or negative timestamps invalidate and retain the previous high-water time;
confirmation restarts only on subsequent progress. A gap exceeding the existing
Observation `staleTimeoutNs` (default 500 ms) restarts confirmation at the next
valid sample. This is existing freshness policy, not a new pose-quality threshold.
Live receiver duplicate/reordered POTB sequences are filtered before the gate;
the offline wrapper applies the equivalent monotonic sequence check. Distinct
received packets are transport samples, not proof of distinct sensor updates.
Native `sourceTimestamp` is diagnostic, not an assumed trustworthy sample clock.

Candidate samples preserve native raw valid=true while effective valid=false.
Capabilities remain `position`, `orientation`, `linear_velocity` on pose and
device-state messages regardless of validity. The gate cannot write source,
device, publisher, tracker, session, clock, coordinate convention, space or
revision. Existing independent sender-session rotation still clears per-device
state and advances revision according to the pre-existing policy. Gate state is
not persisted across process/adapter recreation.

## Raw capture and protocol

The adapter passes the original `PosePacket`, `AdmissionResult`, and effective
Observation header to `admissionDiagnostic`. Enabled-gate unadmitted P/Q and
derivatives are absent from the protocol envelope, while the separate callback
retains raw P/Q. Validity=false, modality=none, evidence=none and tracking=lost
keep shared consumers fail closed. Only admitted P/Q enters the normal path.
No diagnostic consumer can bypass the gate. Callback failure is counted by
`DiagnosticErrors()` and does not enable the sample.

With the gate OFF, previous numeric diagnostic fields, validity, freshness,
modality, capabilities and service latest-per-Pump publishing are preserved.
With the gate ON, the service collects after each accepted packet, so an invalid
packet followed by valid packets in one Pump cannot be hidden by a latest cache.
Legacy POTB fan-outs are compatibility outputs outside the Observation/Core
gate; consumers using those paths do not gain admission. This phase's Core path
is the shared Observation/MTP boundary verified in the integration runner.

`pico_admission_sample` events contain `details`: raw/effective validity, native
flags and tracking-state byte, state/transition/reason, start timestamp, elapsed,
count, raw P/Q and native velocity vectors, position/Q steps, dt, derived speeds,
source timestamp, source/device/session/clock, space/convention/revision,
Observation sequence and immutable timestamp. Native angular frame remains
unverified. Unavailable/nonfinite computed metrics are null; q/-q uses absolute
normalized dot, so it reports the same physical rotation. Jumps and linear
continuation are diagnostic-only and never reject by an unvalidated threshold.

Diagnostics use the existing bounded asynchronous queue (1024 entries, 128
reserved for critical events); per-sample events may drop. Inspect gap/drop and
writer-failure counters before accepting a future HIL capture. There is no new
gate heap allocation, queue, lock, or unbounded history. JSON serialization
allocates only on the explicitly enabled diagnostic path, reusing the existing
writer infrastructure. No quantitative device performance claim is made.

## Deterministic offline tools

Build normally with CMake. `pico_admission_replay` reads JSONL on stdin and writes
one JSONL record per input. An optional first record is `{"config":{...}}`.
Each sample requires `timestamp_ns` (int64), `sequence` (int64), `raw_valid`
(bool), `position` (3 numbers), and `quaternion` (xyzw, 4 numbers). Optional input
includes `raw_orientation_present`, `fresh`, `native_tracking_state`,
`source_timestamp`, `linear_velocity`, `angular_velocity`, `source_id`,
`device_id`, `session_id`, `clock_id` and `coordinate_space`.
The sequence wrapper drops duplicates/reordering. Independent session/space
binding changes discard only that binding's gate, without inventing identity.
Each line is limited to 65535 bytes; the device map is bounded at 256. Processing
streams records without buffering the input/output history.

Output includes all admission diagnostics and the real pinned-codec Observation
in `protocol`. The replay shares production `ApplyPicoAdmission`; it is not a
second implementation of the state machine.

- `scripts/test_admission_replay.py RUNNER`: 12 scenarios, deterministic replay,
  device/sequence/lifecycle isolation, config validation and real mask capture.
- `scripts/import_admission_capture.py SOURCE`: reproducibly extracts the 139
  mask-time frames from the 977-frame 2C-3H controlled-motion capture.
- `scripts/verify_admission_integration.py --workspace ROOT --runner RUNNER
  --output NEW_DIRECTORY`: read-only P1/P1V projections and normalization plus
  two standalone Core replay runs against the c73805da artifact. It requires
  the pinned workspace artifacts, and never starts a live source or endpoint.
  MTP uses an explicit synthetic fixture adapter with the real P1 codec; the
  live Bridge/calibration chain is not claimed as exercised.

The 139 frames are existing Bridge-canonical PICO data captured during an
external mask, with raw pose withheld from Core. They are **NOT natural SLAM
reacquire evidence** and do not select any confirmation/jump/velocity threshold.

## Physical-access boundary

Phase 2C-4A remains NOT STARTED. It requires explicit user confirmation of
physical availability. Its remaining work is natural loss/reacquire capture,
Q/P characterization, duration/sample distributions and threshold tuning,
then real HIL effective-validity and Core recovery validation. Issue #6 remains
separate; software lifecycle checks do not prove HMD sleep/wake recovery.
