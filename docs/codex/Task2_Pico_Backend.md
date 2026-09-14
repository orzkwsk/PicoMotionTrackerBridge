# Codex Task 2 entry point

This file is the entry point for Task 2. Read the following files completely, in order, before making implementation changes:

1. `AGENTS.md`
2. `docs/codex/task2/00_task1_handoff_status.md`
3. `docs/codex/task2/01_backend_scope_and_phases.md`
4. `docs/codex/task2/02_common_contract_and_report.md`

Treat files 01 and 02 together as the normative Task 2 implementation specification. The trailing `<!-- BEGIN COMMON C1 -->` marker at the end of file 01 is only a split boundary; the canonical C1 block begins in file 02.

## Codex Cloud note

If Codex Cloud exposes the writable checkout as an internal branch named `work`, follow the branch exception in `AGENTS.md` and in file 01. Do not stop merely because the managed checkout does not expose the intended GitHub work-branch name. The checkout ancestry must still derive from the required Task 2 base branch.

## Dependency gate

Task 2 Phase A requires the actual Task 1 protocol kit and external handoff manifest. Do not fabricate their hashes or substitute a separately reimplemented schema/codec. The current verified Task 1 source status is recorded in `00_task1_handoff_status.md`.

If the kit has not been supplied into this repository/workspace, you may inspect the existing PICO backend and prepare non-contract-dependent refactoring/tests, but do not report the protocol integration step or Phase A as complete.
