# Firfurcio PAW reference sequence

The seven PNG baselines are cropped from `firfurcio-01.png` through
`firfurcio-07.png`, captured from the original `Supervivencia.tap` in Fuse.
They cover the title, mode-0 picture, message screen, first automatic location
picture, inventory overlay/protected scroll, the second location, and the
room-three PAW pagination prompt.

Fuse's selected Spectrum palette uses `$C0` for normal intensity while ADP's
logical captures use `$D8`; the baselines remap only those palette entries.
Scripted screenshots select the non-inverted FLASH phase, so captures are
deterministic. Comparison against these Fuse captures is exact; any remaining
pixel mismatch is a test failure. Layout, bitmap, attributes, text, fonts, and
protected-scroll placement are all covered pixel-for-pixel.

## Role of this scenario

Unlike every other scenario, these baselines are **external ground truth**:
they were not recorded from ADP output. `fuse-reference/` preserves the
original uncropped Fuse captures they were derived from.

**Never re-record this scenario's baselines.** If it fails after a change,
either the change broke PAW parity, or the change intentionally alters
behavior and must be re-verified against the original tape in an emulator
before these baselines are touched. Re-recording from ADP output would
silently turn this anchor into a self-referential test like all the others.
