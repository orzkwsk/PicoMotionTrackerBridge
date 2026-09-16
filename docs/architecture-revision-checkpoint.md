# Architecture revision software checkpoint

Wire v2 publisher uses the pinned C2 kit (source 572e58cfa20b8b4335207ea5dbcb3f04c587ddff, ZIP a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3). Only paired valid vendor pose becomes FULL; unverified loss capability is NONE. No extra IMU identity.

Receiver defaults: 256 packets/Pump, 256 tracker cache entries, existing retired-session bound retained. Capacity rejects new identities without evicting replay history. Release tests use REQUIRE with unconditional evaluation; historical extraction CI fetches full ancestry.

Validation 2026-09-16: MSVC Release build PASS; CTest 9/9 PASS including bounded receiver and actual UDP. Hardware PICO/axis/loss NOT RUN. Legacy C ABI separate build NOT RUN in this checkpoint; no release/cutover claim.
