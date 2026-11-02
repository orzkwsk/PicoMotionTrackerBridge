# Source-space continuity

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

The final line can instead be `unknown`. A different declared generation
increments revision. Losing continuity increments once and prevents valid PQ;
repeated unknown reads and PC restarts do not keep incrementing. Recovery of
the same generation keeps the invalidated revision. A sender restart with an
already matching declaration of the same generation preserves revision.
Knowledge/revision transitions start a new observation session before resetting
sequence counters. Merely starting an empty receiver does not query a zero
sender or invalidate persisted continuity.

Read the service's `SOURCE_SPACE sender_session=... generation=... revision=...
status=...` log. `scripts/prepare_source_space.py` creates a separate declaration
candidate from the current identity and explicit `same`, `changed` or `unknown`
decision. It never applies that candidate or edits the identity. The operator
must establish continuity; process/session numbers do not prove it. During HIL,
review the candidate and identity SHA, atomically replace the sidecar, then
verify current observations. An already observed unknown interval requires
binding/World/Output review at the new revision. Never manually lower revision.

Normal PC startup is `../launchers/Start_PicoMotionTrackerBridge.cmd`, reading fixed repository
`Build/runtime/Release` native output and `Build/runtime/control/Release` GUI output.
`python scripts/build_runtime.py` builds them explicitly. `--check` starts no
executable and reports artifacts separately from current-user identity/route state.
It opens the control GUI and its receiver-owning tray. The only launcher option is
`--check`. Old repository entries and runtime/artifact/HMD launch helpers have been
removed. The check reports this process's `LOCALAPPDATA`; sandbox state may differ
from the desktop user's state. It never copies, creates or rewrites that state.
Normal startup never invokes ADB, registration, or a build. Window X retains tray
and service; Exit gracefully stops only its owned service and then closes the tray.
