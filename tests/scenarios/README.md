# Scenario Format

Each scenario lives in `scenarios/<game>/<scenario>/scenario.json`, alongside
its input script and expected transcript/checkpoints. The host runner accepts:

```json
{
  "fixture": "example/target/BOOT.DSK",
  "part": 1,
  "input": "playthrough.input",
  "transcript": "expected.txt",
  "timeout_seconds": 30
}
```

`part` is optional. When set (1-based), the fixture is booted through the real
player so its part selector appears; the runner auto-selects that part instead
of waiting for a keypress and captures the selector prompt to `part-selector.png`
beside the scenario. Use it for multi-part games (each part is a separate
scenario sharing one disk image) and to guard the loading/selector screen, which
is easy to break. A scenario without `part` runs the first database on the image
as before.

`produces` and `consumes` wire up games that hand state between parts through a
saved-position file on disk (e.g. Cozumel: part 1 saves at the boundary, part 2
loads it at boot). Give the producing scenario `"produces": "PART1ENDING.SAV"`
(a name or list); the runner records that file as a byte baseline beside the
scenario and compares it on every run, so the part-boundary game state is
checked. Give the consuming scenario `"consumes": { "from": "cozumel/part1-amiga",
"file": "PART1ENDING.SAV" }` (or a list of such entries); before it runs, the
runner copies the producer's recorded baseline into the working directory so the
game's LOAD finds it. The consumer reads the producer's committed baseline, so it
runs standalone without re-running the producer.

`screen` is optional and selects the graphics mode for platforms that support
several — the IBM PC builds in `cga`, `ega`, or `vga`. It is passed to the player
as `--screen`, so it also applies through the part selector. Because each mode
renders different graphics, give each one its own scenario (see sharing below);
the transcript is the same across modes.

## Sharing baselines across platforms

The same game on different 16-bit platforms usually plays through with an
identical transcript, and often identical in-game graphics; only the fixture
(and sometimes the loading screen or other graphics) changes. Rather than
duplicate the input, transcript, and captures, a platform can inherit them with
`shared`:

```json
{
  "fixture": "chichen/atarist/CHICHEN-ST.st",
  "shared": "chichen/part1-amiga"
}
```

`shared` names another scenario (relative to `scenarios/`). Its scenario.json
fields (`part`, `input`, `transcript`, `timeout_seconds`, `screenshot`) fill in
as defaults, and every baseline file — the input script, transcript, and each
screenshot — is resolved from the local scenario directory first, then from the
shared one. So the shared scenario owns the common baselines and the inheriting
scenario keeps only the files that genuinely differ.

Recording (`RECORD=1`) sorts this out automatically: for each transcript and
screenshot it compares the run against the shared baseline and writes a local
copy only when they differ, deleting a stale local copy when they match. Record
the shared (base) scenario first, then record each platform variant — a variant
whose output matches entirely ends up with just its `scenario.json`. Platforms
that do differ keep their own captures while still sharing the transcript when
the text lines up.

The IBM PC modes are a good example of the range. Chichen on the Atari ST shares
everything but its loading screen with the Amiga base. The PC builds share the
transcript with the same base but each `screen` mode (`cga`, `ega`, `vga`) draws
its own graphics, so each mode's scenario records its full set of captures; the
8-bit versions behave the same way. In every case the 45 KB transcript and the
input script live once.

`input` is an ADP scripted-input file. The scenario runner removes blank or
whitespace-only lines and lines whose first non-whitespace character is `#`, so
they can organize a long solution. Other non-directive lines are game commands.
Use `@key` on its own line to send Return to a pause or key prompt. A pause also
receives a synthetic Return when the next script item is a command, so the
command remains queued for the next input prompt. The script must reach a clean
game exit.

`FAST=1` (which passes `--skip-timed-pauses`) is the recommended way to validate
a change or check for regressions. It skips the timed `PAUSE` waits and the
frame-sync delays but still renders every frame, so it produces the same
transcript and the same screenshots as a real-time run — a full playthrough
compares in about a second instead of tens of seconds. Because `@save`/`@capture`
only fire while the game waits for input, the framebuffer is settled and
identical at every checkpoint regardless of speed. The one exception is
recording: capture baselines with `RECORD=1` and *without* `FAST=1`, so the
authoritative frames reflect the game's real timing and animations.

Add `@save <name>` (or `@capture <name>`) on its own line after a command or
pause when the game is waiting for its next input. The runner prefixes each
capture automatically with its zero-based sequence number, such as
`00-Start.png`, and compares it against that file beside the scenario. Include
`.png` in the directive to use an explicit filename. Captures have no window
borders or display scaling.

Validate one scenario (the normal way to check a change or catch a regression):

```sh
make -f Makefile-linux test-game SCENARIO=chichen/part1-amiga FAST=1
```

Drop `FAST=1` to run it at the game's real speed; the result is the same, just
slower.

Generate or intentionally replace its transcript and capture baselines with
(record at real timing, without `FAST=1`):

```sh
make -f Makefile-linux test-game SCENARIO=chichen/part1-amiga RECORD=1
```

Enable the interpreter trace in `test-results/scenarios/<game>/<scenario>/trace.log`
only while diagnosing a scenario:

```sh
make -f Makefile-linux test-game SCENARIO=chichen/part1-amiga TRACE=1
```

For manual investigation, start the game with a real SDL window and disable the
scenario timeout:

```sh
make -f Makefile-linux test-game SCENARIO=chichen/part1-amiga INTERACTIVE=1
```

This hands control to the player after the script ends. Place `@interactive` on
its own line to hand control over at that exact point instead; it requires
`INTERACTIVE=1`. `@stop` is an equivalent explicit breakpoint: it preserves
the rest of the input script but stops consuming it and hands control to the
player.

Combine it with tracing when investigating a live run:

```sh
make -f Makefile-linux test-game SCENARIO=chichen/part1-amiga INTERACTIVE=1 TRACE=1
```

The live game window remains interactive and the interpreter trace is written
to `test-results/scenarios/chichen/part1-amiga/trace.log` continuously while the
game runs.

## Non-redistributable fixtures

Some games (e.g. Zenobi releases) are free to download but may not be
redistributed, so their tape/disk images cannot live in this repository.
Commit everything that is ours (scenario.json, input, transcript, baseline
captures) and reference the game itself with two extra scenario fields:

    "fixture": "zenobi/somegame/GAME.tzx",
    "fixtureUrl": "https://authorized.source/GAME.tzx",
    "fixtureSha256": "<sha256 of the exact file>",

On first use the runner downloads the file from the authorized source into
the normal fixture path and verifies the hash. A failed download SKIPs the
scenario (offline runs stay usable); a hash mismatch FAILs it loudly. Add
each such path to tests/games/.gitignore so the downloaded file
can never be committed by accident.
