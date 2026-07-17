# Firfurcio completion scenario

This scenario proves that the supplied game can be played through to its
ending and retains ADP regression captures at selected milestones. Those
captures are **not** emulator goldens and must not be used to claim PAW visual
parity.

The emulator-derived startup, text-control, automatic-picture, inventory,
protected-scroll, and second-location goldens live in `firfurcio-startup`.
Later completion captures should move to that reference class only after a
matching original-PAW/Fuse frame has been recorded and reviewed.

The emulator-backed startup scenario contains the explicit `@waitkey` assertion
for the room-three PAW pagination wait. This longer completion script exercises
additional text that can legitimately exhaust `SCR_CT` earlier, so its incidental
pagination waits use the test driver's non-consuming synthetic acknowledgement.

Scripted screenshots select a fixed, non-inverted FLASH phase, so both
real-time and skipped-pause runs compare exactly.
