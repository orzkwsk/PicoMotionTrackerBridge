# Source-space diagnostics and normal Pose

Issue #17 (2026-10-30) supersedes the old use of continuity declarations as
output permission. A fresh native finite FULL Pose remains usable without a
`.space` sidecar, physical evidence or approval. Known units/axes remain
`pico-output-a-v1`; native position/orientation validity, quaternion sanity,
reference frame, session ordering and freshness are still required. Unsupported
rotation-only semantics are not inferred. Bridge owns saved W/M and retains
them across Backend/transport restarts; unknown calibration quality is displayed
as uncalibrated and does not make normal Pose invalid.

The inspected POTB receiver and PICO backend contract expose sender sessions,
but do not supply a trustworthy coordinate-space generation indicator. The PC
service therefore keeps session generation separate from explicit space state.
It never calls AdvanceRevision merely because the provider was constructed.

`observation-provider.conf` v2 persists source, space, revision, declared
generation and known/unknown state. v1 identity/revision is preserved on read;
its continuity starts unknown until declared. State replacement is a unique
temporary write, flush and atomic replace. Partial abandoned files are ignored.
Corrupt state and revision overflow fail closed. The service owns its instance
mutex for the identity's lifetime.

The sidecar `<identity>.space` format is:

```
pico-source-space-declaration-v1
<exact source ID>
<exact input-space ID>
<independent coordinate-space generation>
<current POTB sender session, unsigned decimal>
known
```

The final line can instead be `unknown`. A different explicitly declared
generation increments the diagnostic revision. Losing a declaration or changing
transport does not advance the saved map revision. Recovery of the same
generation preserves revision. Missing, unreadable or malformed optional
sidecars mean unknown quality; they do not disable normal Pose. A saturated
diagnostic revision does not wrap or replace identity: a new declaration remains
unknown. Explicit identity-generation writes still reject overflow/corruption.
Failure to save optional quality metadata preserves the durable identity and
generation, reports `metadata=not_persisted` with unknown quality, and leaves
normal Pose available. Initial identity creation and corrupt identity still
fail instead of silently creating/replacing the source.

An actual recorded revision transition starts a new observation session before
resetting short-lived sequence/derivative/admission history. A quality-only
known/unknown transition does not rotate the live sample session. Sender restart
still starts a fresh observation session and drops old samples. Merely starting
an empty receiver does not query a zero sender or mutate saved map state.

Read the service's `SOURCE_SPACE sender_session=... generation=... revision=...
status=...` log. `scripts/prepare_source_space.py` creates a separate declaration
candidate from the current identity and explicit `same`, `changed` or `unknown`
decision. It never applies that candidate or edits the identity. The operator
may record continuity for diagnostics; process/session numbers do not prove it.
This is optional and not a normal startup, Pose, calibration or HIL-collection
prerequisite. Users may inspect drift and explicitly recalibrate W/M when
needed, without a separate Output Approval session. Never manually lower a
saved revision or rewrite stable source/device IDs to align calibration.

Normal PC startup is `Start_Receiver.cmd`, selecting the latest successful
compatible artifact set. The four old Start_* entry points are deprecated and
exit without actions. Build scripts are explicit developer tools. Normal
startup never invokes ADB, registration, or a build. Window X retains tray and
service; Exit gracefully stops only its owned service and then closes the tray.
