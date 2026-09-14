# PicoMotionTrackerBridge repository instructions

This repository owns the PICO-specific backend boundary: private runtime/ABI access, enumeration, serial/pose/metadata acquisition, device-clock handling, the HMD publisher, the PC receiving shim, and conversion into MonakaVR TrackerObservation messages.

## Codex task entry point

For the current MonakaVR layer-separation work, read `docs/codex/Task2_Pico_Backend.md` completely before making implementation changes. Treat that document as the normative Task 2 specification.

## Codex Cloud branch handling

Codex Cloud may expose its writable checkout as an internal branch named `work` rather than the GitHub branch name selected as the environment base. This is allowed.

For Task 2, the required GitHub base is `refactor/pc-bridge-service-fanout`. Verify that the checkout HEAD descends from the audited base SHA recorded in the task document (or from a later reviewed fast-forward of that branch). Do not fail solely because the local writable branch is named `work`, and do not attempt to recreate or switch to `refactor/monaka-layer-separation` inside a Codex-managed checkout when remote branches are not exposed.

If the checkout does not derive from the required base branch, stop and report the mismatch instead of implementing against `main` or another branch.

## Repository-wide constraints

- Work only in this repository unless the task specification explicitly states otherwise.
- Preserve the PICO private runtime/ABI boundary and existing build-ID guards.
- Do not redefine the Monaka protocol schema or hand-roll an incompatible codec. Task 1's protocol kit is the contract dependency.
- Do not move mapping/calibration, generic output routing, body-role assignment, Fusion/Fallback/IK, or common SteamVR ownership into the PICO backend.
- Preserve unrelated existing work and uncommitted changes.
- Do not delete branches, force-push, rebase, amend, reset away work, or otherwise rewrite history.
- Keep implementation commits small and reviewable.
- Run applicable build/tests before reporting completion and clearly mark NOT RUN items.
- Do not claim hardware validation when it was not performed.
- Phase A may prepare provider/extraction work, but do not fabricate Task 1 artifacts or hashes if the protocol kit has not actually been delivered.

If repository state or a dependency conflicts with the task document, inspect and report the difference rather than overwriting or silently adapting the contract.
