# Game Fixtures

This directory contains the redistributable regression-game corpus used by
ADP's automated compatibility tests. The project asserts that these releases
are public domain and may be redistributed for this purpose.

Game media is stored under `tests/games/<game>/<platform>/`; original filenames are
preserved because disk loaders and multi-disk games depend on them. Scenario
source and its expected transcript/checkpoints are stored separately under
`scenarios/<game>/<scenario>/`.

Only source-controlled test inputs and approved baselines belong here. The test
runner writes generated transcripts, captures, logs, and image diffs to
`test-results/scenarios/<game>/<scenario>/`.
