# Task 2 Phase A implementation and handoff

## Baseline and contract

The initial Desktop checkout was clean main. The Task 2 documents were on
`origin/refactor/pc-bridge-service-fanout`, not main. `git fetch --no-prune origin`
resolved that required base to `22c7bd8cdbd9a52355b6ff182f0ddc462a998bd8`.
It descends from audited `4c0321f02b56a8ce420ba40a988ca9c296f05f61`; the intervening
five-file diff is Task 2 documentation only. The new local
`refactor/monaka-layer-separation` branch was created from that reviewed base.
The Codex Cloud `work` exception was unnecessary. No other branches were
deleted or rewritten, and main was not used for implementation commits.

Unlike the older handoff-status snapshot, this workspace contains the real
Task 1 ZIP and external manifest produced by Task 1. Both were read as
artifacts, not rebuilt from a sibling live checkout. The verified ZIP is
`eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729`.
Source commit is `5f41586b51bd84bb1cea344879b5d6325fc2c47d`, schema commit is
`04f2d6c831c68a65edfbfb66ff8838d1c9d78535`, and C1 SHA256 is
`3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3`.
The original archive, external manifest and complete lock are under
`dependencies/`; all kit bytes are under `third_party/monaka-protocol/`.
`python scripts/import_protocol_kit.py --verify` verifies the whole handoff
and compares Task 2's C1 text with the kit. The contract remains a candidate;
master reconciliation was not performed and no schema/codec was reimplemented.

## Provider behavior

`PicoObservationAdapter` reads accepted Receiver states directly. It does not
use the compatibility `ToTrackerState()` conversion or `posePcMonotonicNs`.
The C1 clock has a fixed PC process epoch; `lastPoseReceivePcNs - epoch` is
computed on admission. Neither metadata nor clock resynchronization emits an
old pose or changes its sequence/timestamp. `sent_at_ns` is stamped immediately
before encoding in the same clock. Sequence streams are independent per serial
and message type; POTB uint32 wrap does not wrap the C1 signed U63 counter.

The service retains the only HMD ingress on 29765. It opts into exclusive bind
and retired-session rejection. Legacy Receiver users retain their existing
default socket/session policy. A stopped C1 consumer has a separate loopback
ephemeral nonblocking socket, cannot block legacy publishers, and does not
cause a retry queue to accumulate. Failed C1 datagrams are counted and dropped;
later accepted samples continue. Provider exceptions disable only C1 and are
logged; old routing continues. The adapter is bounded at 256 device entries;
the receiver's optional retired-session guard stops new sessions after 1024
retirements instead of evicting identities and accepting stale replay.

Mapping preserves metres and original xyzw values in `pico-output-a-v1`.
Position and orientation validity are independent; finite but invalid position
remains diagnostic, and sane orientation uses `sample_sanity`. Angular velocity
and acceleration remain null. Linear velocity uses a measured space derivative
only with valid position and the known PICO Output A frame. Unknown frame codes
are rejected, never given an identity transform. No raw state, bucket, runtime
ID or candidate derivative escapes as an extension field.

Metadata presence and tracking state remain `unknown`: connectedSnapshot is
an enumeration snapshot. Fraction is null; charging is also null because POTB
does not carry a live-validity signal for its cached enumeration charging bit.
No new vendor metadata probe is introduced. Pose timeout remains consumer
policy (500 ms initially); delayed new samples carry their original admission
age and invalid components. Query success is still not sensor freshness proof.

## Identity and operation

The existing service now also sends C1 to `127.0.0.1:29810` by default,
independently of its old output route. `--observation-port PORT` changes the
destination; old 29765-29768 ports are rejected. `--observation-config PATH`
selects the identity file, whose default is:
`%LOCALAPPDATA%/PicoMotionTrackerBridge/observation-provider.conf`.

The four-line UTF-8 file contains:

```text
pico-observation-identity-v1
<persistent-source-uuid>
<configured-space-id>
<uint32-revision>
```

A first installation creates a random UUID source and a source-specific map
ID. Every PC service lifetime advances/persists revision before publishing.
An accepted POTB sender change generates a new C1 session UUID, advances the
revision again and drops provider progress/cache. The clock ID remains fixed
for that PC clock epoch. Revisions fail closed at uint32 exhaustion. Identity
files are replaced atomically while the existing service instance mutex is
held; a malformed file disables C1 rather than silently replacing identity.
Stop the service before editing the map ID or moving an identity file. Do not
copy one source identity to another installation. A new revision requires
Bridge recalibration; no transform or body role is owned here.

Phase A retains 29766 SteamVR, 29767 Monaka and 29768 Utility compatibility,
GUI/driver/calibration targets, and the Monaka receiver C API. When testing the
new Bridge concurrently, disable one driver's output for the same trackers;
do not publish the same physical tracker through both old and new drivers.
No launcher, driver registration, HMD app deployment or hardware session was
performed by this implementation task.

## Task 4 extraction

`python scripts/package_bridge_extraction.py` exports exact legacy files from
the reviewed base into `dist/pico-bridge-extraction.zip`. It includes all
SteamVR output sources, calibration and C# UI sources, scripts, routing/IPC
headers, original CMake reference, normalized transport/type include closure,
output docs, and generated config examples/migration instructions. PICO private
runtime/ABI/Android implementation is excluded. The generated standalone CMake
builds the legacy transport and optional Windows driver without a sibling repo.
The root original CMake is preserved unchanged under `reference/`.

Each original file records repository, path, base SHA, git blob SHA and SHA256.
Generated files are explicitly labelled and have no invented original blob.
The manifest lists include edges, destination candidates, license status,
external SDK/toolchain requirements and old-type replacement points. No root
LICENSE/NOTICE was present at the source base; existing source notices remain
byte-identical. SDKs and installed runtimes are not falsely listed as bundled.

Run `python scripts/verify_bridge_extraction.py` for hash/provenance/closure
checks, or add `--build --cmake <path>` to build/test in a fresh temporary
workspace. Final hashes and the actual isolated-build result live in the
external handoff report, avoiding circular ZIP/source hashes.

## Verification and remaining gates

Windows host Debug build and all 8 CTest cases passed, including the shared-kit
integrity check and POTB -> provider -> C1 -> Task 1 decoder mock. Tests cover
partial validity, unknown metadata, candidate exclusion, duplicate/out-of-order
packets, uint32 wrap, sender change/retired replay, actual clock resync with
unchanged age, metadata-only continuation, stopped consumer/old-path progress,
single bind, eight devices and a bounded three-device configuration, restart
identity/revision and encoding-time stamping.

The existing SteamVR build script passed driver/calibrator and all 8 tests.
The existing Android script built arm64 native libraries and app-debug.apk
using Unity 2022.3.22f1, NDK 23.1.7779620, Clang 12.0.8, Gradle 7.2 and AGP 7.1.2.
Android build rules contain no Monaka/Observation/JSON target. PICO runtime,
internal decoder/clock mapper, Backend C API and Android application sources
are unchanged from base, including all Build-ID/ABI guards and prohibited paths.
The separate legacy C API build/test and extraction build are recorded in the
final external report after completion.

Sandbox MSVC discovery/environment and NDK detection attempts initially failed
or stalled; approved runs succeeded. One automatic approval review failed due
to its usage limit; a later reviewed retry succeeded. CMake/NDK/Gradle emitted
existing deprecation and read-only SDK metadata warnings. These are not tracker
validation. Linux host execution and hardware are NOT RUN. No claim is made
for five physical trackers, occlusion, power cycles, Virtual Desktop coexistence
or new-Bridge Direct/MTP runtime equivalence.

Phase B is not started. It requires Task 4 Bridge HEAD/hash and Direct regression,
Task 5 consumer HEAD/hash and proof that the receiver C API is unused, plus
configuration migration checks. Historical enumeration/leak/query-vs-sensor
and firmware limitations remain. Do not label Phase A as Backend separation.
