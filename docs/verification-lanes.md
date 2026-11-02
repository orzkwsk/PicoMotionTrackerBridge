# Verification lanes — input producer

The producer reports only observed data and truthful validity. It is not the owner of user-selected body constraints or optional world calibration acceptance.

- `powershell -File scripts/test_revision.ps1 -Mode Quick`: compile and run PICO observation, admission, recovery/receiver and loopback tests, excluding source-kit integrity and old space-authorization-policy tests.
- `powershell -File scripts/test_revision.ps1 -Mode Full`: the functional native suite plus Monaka compatibility C API build/test; legacy source-kit and old space-policy tests remain outside this lane.
- `powershell -File scripts/test_revision.ps1 -Mode Audit`: complete historical native/C API suite, including frozen contract artifacts, source-space policy expectations and release-tooling tests.

These are **software** checks and do not prove optical tracking, native sample freshness, validity semantics, correct sensor units or HIL behavior. The runtime source-space admission policy is still present pending MonakaBridge#17 and related coordinated changes. Current software PASS must not be presented as successful policy remediation.

When changing runtime behavior, preserve truthful vendor validity and communication handling; do not continue to block valid observations solely because coordinate continuity was not independently certified by an operator. Use functional Pose traces and realistic failure/reacquisition cases as the primary regression proof; run source integrity and release evidence when those specific boundaries change.
