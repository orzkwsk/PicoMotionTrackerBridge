# PicoMotionTrackerBridge repository instructions

This repository owns the PICO-specific backend boundary: private runtime/ABI access, enumeration, serial/pose/metadata acquisition, device-clock handling, the HMD publisher, the PC receiving shim, and conversion into MonakaVR TrackerObservation messages.

## Current coordinated Architecture Revision

The older Task 2 document and its fixed C1/wire-1.0 handoff are historical baseline material for the already-delivered implementation. For the current coordinated revision, protocol/fallback/identity semantics are superseded by the Architecture Revision owned in `MonakaVR/MonakaProtocol` (`docs/architecture-revision.md` and `docs/C2.md`). Do not revert current v2 semantics merely to satisfy an older Task 2 paragraph.

Protocol-dependent migration work must use an **actual generated and hash-pinned wire-v2 kit** from a clean committed MonakaProtocol source tree. Until that artifact and manifest are supplied to this workspace, inspect/plan freely but stop at the protocol dependency boundary; do not fabricate hashes, infer v2 fields from v1, or hand-roll a substitute codec.

Where the current Architecture Revision does not conflict, the existing Task 2 hardware/backend constraints and historical evidence remain applicable.

## Codex task entry point

For backend history and non-conflicting PICO-specific constraints, read `docs/codex/Task2_Pico_Backend.md` completely before making implementation changes. Treat its fixed-C1 sections as historical once the coordinated v2 handoff is active.

## Codex Cloud branch handling

Codex Cloud may expose its writable checkout as an internal branch named `work` rather than the GitHub branch name selected as the environment base. This is allowed.

For the historical Task 2 base, the required GitHub base was `refactor/pc-bridge-service-fanout`. For the current coordinated revision, continue only from the prepared `refactor/monaka-layer-separation` lineage (or a managed `work` checkout derived from it). Do not fail solely because the local writable branch is named `work`, and do not rewrite history to recreate branch names.

If the checkout does not derive from the prepared lineage, stop and report the mismatch instead of implementing against `main` or another branch.

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
