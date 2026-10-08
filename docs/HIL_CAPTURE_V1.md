# Standalone HIL datagram capture v1

Build host targets with CMake and run:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tests/capture_cli_test.py build/Release/pico_bridge_capture_cli.exe -v
./build/Release/pico_bridge_capture_cli.exe --port 29765 --output CAPTURE.ndjson --duration-sec 30 --flush-interval 1000
```

The CLI reuses the wire decoder and UDP transport, independently of Receiver
latest-state storage and SteamVR. Stop the production Receiver first. A new
opt-in `OpenExclusive` socket helper uses Windows SO_EXCLUSIVEADDRUSE; normal
Open callers keep their prior semantics. No production Backend, Poll, ClockMapper,
Publisher, Receiver state, wire format or SteamVR output behavior changes.

Every datagram becomes one schema v1 NDJSON record: pose, device_state,
decode_error, time_sync_request or time_sync_response. All records include raw
pc_stopwatch_ticks/frequency, pc_clock_kind, pc_utc and wire_size_bytes. Windows
timestamps are raw QueryPerformanceCounter/Frequency immediately after
ReceiveFrom returns and before decoding, the same domain as .NET Stopwatch on
the PC. MonotonicNowNs is not used. POSIX standalone tests use CLOCK_MONOTONIC
and declare that separate kind; they cannot be silently adopted as Windows HIL
run-host timestamps.

Pose values keep native serial/runtime identity, session, global wire sequence,
source_timestamp_ns, hmd_observed_monotonic_ns, hmd_mapped_monotonic_ns, independent
position_valid/orientation_sample_present/mapped_monotonic_valid flags, raw state,
position_m, orientation_xyzw, linear_velocity_m_s, angular_velocity_rad_s and frame
enums. Device state keeps identity/session/sequence, connection, charging, battery
presence/raw bucket and observed HMD monotonic time. Controls preserve nonce and
their native timing fields without initiating time synchronization. Decode
errors keep status and wire size; no raw bytes or peer address are saved.

No quaternion reordering, coordinate conversion, pose validity classification,
session conversion or timestamp fitting occurs. Finite binary64 values use
max_digits10. Native uint64 values are decimal JSON integers. JSON escaping
preserves control characters and valid UTF-8 serials within the 31-byte wire
limit. Invalid UTF-8 serial bytes are reported as invalid_serial_utf8 rather
than emitting invalid JSON. Existing wire-numeric errors become decode_error.

Output creation is exclusive; existing captures cannot be overwritten. A 1 MiB
stdio buffer writes sequentially. --flush-interval is milliseconds (1..60000,
default 1000); --duration-sec is integer seconds (0=unbounded, up to 86400).
No per-packet flush/stdout/metadata call. Metadata-based stop polling runs at
most every 100 ms, including under continuous traffic. SIGINT/SIGTERM or duration
expiry flush/close before exit. Status/errors go only to stderr.

HIL orchestration may supply --expected-frequency, --stop-file, --ready-file and
--completion-file. Readiness follows successful initialization; completion is
written only after successful file close. Windows paths use UTF-16 argv and
wide filesystem open. Preserve failed captures/logs locally. Stop capture before
hilctl stop seals raw SHA256. The HIL template config is generated from each
run's QPC frequency, validates every captured frequency, and uses existing
structured-ndjson. Global wire_sequence is retained only in raw payload; never
map it to normalized tracker-stream sequence. Raw payload must never be committed
or sent to reduced evidence.

Host CI runs existing C++ tests plus encode/decode/format and actual UDP tests on
Windows and Linux. .NET QPC correlation, UTF-8 escaping, independent validity,
64-bit IDs/timestamps, nonfinite packets, zero-size/malformed packets, exclusive
bind, overwrite rejection, timed/graceful stop and 5 trackers x 120 Hz are covered.
Assertion-based existing tests keep assertions active in Release test targets.
The local formatting smoke observed ~10.4 us/Pose record; the UDP smoke recorded
1200/1200 datagrams in a paced 2-second synthetic input. Neither establishes
real-device timing overhead or a real-time guarantee. Keep the feature unmerged
until the first physical HIL scenario is reviewed.
