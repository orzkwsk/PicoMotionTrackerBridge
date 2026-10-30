# PicoMotionTrackerBridge repository instructions

This repository owns the PICO-specific backend boundary: private runtime/ABI access, enumeration, serial/pose/metadata acquisition, device-clock handling, the HMD publisher, the PC receiving shim, and conversion into MonakaVR TrackerObservation messages.

## Current coordinated Architecture Revision

The older Task 2 document and its fixed C1/wire-1.0 handoff are historical baseline material for the already-delivered implementation. For the current coordinated revision, protocol/fallback/identity semantics are superseded by the Architecture Revision owned in `MonakaVR/MonakaProtocol` (`docs/architecture-revision.md` and `docs/C2.md`). Do not revert current v2 semantics merely to satisfy an older Task 2 paragraph.

Protocol-dependent migration work must use an **actual generated and hash-pinned wire-v2 kit** from a clean committed MonakaProtocol source tree. Until that artifact and manifest are supplied to this workspace, inspect/plan freely but stop at the protocol dependency boundary; do not fabricate hashes, infer v2 fields from v1, or hand-roll a substitute codec.

Where the current Architecture Revision does not conflict, the existing Task 2 hardware/backend constraints and historical evidence remain applicable.

## Codex task entry point

For backend history and non-conflicting PICO-specific constraints, read `docs/codex/Task2_Pico_Backend.md` completely before making implementation changes. Treat its fixed-C1 sections as historical once the coordinated v2 handoff is active.

## Branch policy and Codex Cloud handling

The repository now uses `dev` as the normal integration/development branch.

- `main` is the stable/release baseline. Do not use it as the default implementation base for ongoing development.
- `dev` is the current development integration line. New `feature/*`, `fix/*`, `refactor/*`, and `diagnostic/*` work should normally branch from the current `dev` HEAD and merge back to `dev`.
- `dev` was established from the prepared Monaka layer-separation lineage after the HIL backend/APK synchronization work. The older `refactor/monaka-layer-separation` branch remains as historical/coordinated-work lineage and must not replace `dev` as the default base.
- Existing topic branches remain valid history. Do not rewrite, rebase, delete, or force-update them merely to fit the current branch policy.
- An active task may explicitly pin another branch or commit. A task-specific pinned base overrides the normal `dev` rule only for that task.

Codex Cloud may expose its writable checkout as an internal branch named `work` rather than the GitHub branch name selected as the environment base. This is allowed. For ordinary new work, the checkout must derive from current `dev` (or from an explicitly pinned task base). Do not fail solely because the local writable branch is named `work`, and do not rewrite history to recreate branch names.

If a normal new-work checkout does not derive from `dev`, stop and report the mismatch instead of silently implementing against `main` or an obsolete topic branch.

## Product-software verification and historical audit (2026-10-30)

MonakaVR is a producer-independent SlimeVR 6DoF constraint extension, not a physical-tracker quality certification tool. Normal software PASS must demonstrate actual public-input handling, transforms, IK/fallback/output or the relevant backend function; version/source hashes and historic approvals do not substitute for that behavior.

`scripts/test_revision.ps1` uses `-Mode Quick` and `-Mode Full` for product-software checks and `-Mode Audit` for historical source/release-policy checks. Audit may be used for a relevant release, dependency or policy migration, but must not be imposed as a default per-edit requirement. See `docs/verification-lanes.md`. Software PASS is not hardware HIL PASS. The existing mandatory calibration approval/revision model is a migration target under MonakaVR/MonakaBridge#17, not the intended long-term product acceptance criterion.

## Repository-wide constraints

- Work only in this repository unless the active coordinated task explicitly states otherwise.
- Preserve the PICO private runtime/ABI boundary and existing build-ID guards.
- Do not redefine the Monaka protocol schema or hand-roll an incompatible codec. MonakaProtocol owns the contract dependency.
- Do not move mapping/calibration, generic output routing, body-role assignment, Main/Fallback policy, Fusion/Fallback/IK, or common SteamVR ownership into the PICO backend.
- Backend output may report only modality/capability/loss semantics actually supported by PICO evidence. Unsupported rotation-only behavior must fail closed rather than be inferred from stale numeric pose fields.
- Preserve unrelated existing work and uncommitted changes.
- Do not delete branches, force-push, rebase, amend, reset away work, or otherwise rewrite history.
- Keep implementation commits small and reviewable.
- Run applicable build/tests before reporting completion and clearly mark NOT RUN items.
- Do not claim hardware validation when it was not performed.

If repository state, the active v2 kit, or observed hardware behavior conflicts with the current Architecture Revision, inspect and report the difference rather than overwriting or silently adapting the shared contract.
