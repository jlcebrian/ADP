#!/usr/bin/env python3
"""Manifest-driven regression runner for redistributable ADP game fixtures."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures"
GAMES = FIXTURES / "games"
SCENARIOS = FIXTURES / "scenarios"
RESULTS = ROOT / "test-results"
IMAGE_EXTENSIONS = {".adf", ".dsk", ".st", ".tap", ".tzx", ".cas", ".d64", ".t64", ".cdt"}


@dataclass(frozen=True)
class Probe:
    fixture: Path
    command: tuple[str, ...]


def load_manifest() -> dict:
    with (FIXTURES / "manifest.json").open(encoding="utf-8") as source:
        return json.load(source)


def discover_inventory(selected: set[str]) -> list[Probe]:
    probes: list[Probe] = []
    for fixture in sorted(GAMES.rglob("*")):
        if not fixture.is_file() or fixture.suffix.lower() not in IMAGE_EXTENSIONS:
            continue
        game = fixture.relative_to(GAMES).parts[0]
        if selected and game not in selected:
            continue
        probes.append(Probe(fixture, ("list", str(fixture))))
    return probes


def discover_probes(manifest: dict, selected: set[str], inventory: bool) -> list[Probe]:
    if inventory:
        return discover_inventory(selected)

    probes: list[Probe] = []
    for relative in manifest.get("probes", []):
        fixture = GAMES / relative
        game = Path(relative).parts[0]
        if selected and game not in selected:
            continue
        if not fixture.is_file():
            raise FileNotFoundError(f"Missing fixture listed by manifest: {relative}")
        probes.append(Probe(fixture, ("list", str(fixture))))
    return probes


def run_probe(ddb: Path, probe: Probe, results: Path) -> bool:
    name = probe.fixture.relative_to(GAMES).as_posix().replace("/", "__")
    log = results / "probe" / f"{name}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        [str(ddb), *probe.command], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
    )
    log.write_text(completed.stdout, encoding="utf-8")
    if completed.returncode == 0:
        print(f"PASS probe {probe.fixture.relative_to(ROOT)}")
        return True
    print(f"FAIL probe {probe.fixture.relative_to(ROOT)} (see {log.relative_to(ROOT)})")
    return False


def normalize_transcript(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def decode_tool_output(output: bytes | str | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("latin-1")
    return output


def as_list(value) -> list:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def run_scenario(ddb: Path, scenario_path: Path, results: Path, record: bool = False, trace: bool = False, interactive: bool = False, skip_timed_pauses: bool = False) -> bool:
    try:
        scenario = json.loads(scenario_path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: scenario.json is missing")
        return False
    except json.JSONDecodeError as error:
        print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: scenario.json is empty or invalid ({error})")
        return False
    base = scenario_path.parent

    # A scenario may inherit baselines from another platform's scenario via the
    # "shared" key: identical input, transcript, and in-game captures live once in
    # the shared scenario, and this one keeps only the files that differ (e.g. the
    # platform-specific loading screen). Shared scenario.json fields fill in as
    # defaults; local fields and locally-present files always win.
    shared_base: Path | None = None
    if scenario.get("shared"):
        shared_scenario_path = SCENARIOS / scenario["shared"] / "scenario.json"
        if not shared_scenario_path.is_file():
            print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: shared scenario not found: {scenario['shared']}")
            return False
        shared_base = shared_scenario_path.parent
        shared_fields = json.loads(shared_scenario_path.read_text(encoding="utf-8"))
        for key in ("part", "input", "transcript", "timeout_seconds", "screenshot", "produces"):
            if scenario.get(key) is None and key in shared_fields:
                scenario[key] = shared_fields[key]

    required = ("fixture", "input", "transcript")
    missing = [key for key in required if not scenario.get(key)]
    if missing:
        print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: missing {', '.join(missing)}")
        return False

    def baseline(name) -> Path:
        """Resolve a baseline file: the local scenario dir first, then the shared one."""
        local = base / name
        if shared_base is None or local.exists():
            return local
        shared = shared_base / name
        return shared if shared.exists() else local

    # A multi-disk game is supplied as several images, so "fixture" may be a list;
    # they are all mounted at once and listed in disk order (disk 1 first).
    fixture_spec = scenario["fixture"]
    fixture_names = fixture_spec if isinstance(fixture_spec, list) else [fixture_spec]
    fixtures = [GAMES / name for name in fixture_names]
    input_path = baseline(scenario["input"])
    expected_path = baseline(scenario["transcript"])

    def fail(reason: str) -> bool:
        print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: {reason}")
        return False

    for fixture in fixtures:
        if not fixture.is_file():
            return fail(f"fixture not found: {fixture.relative_to(ROOT)}")
    if not input_path.is_file():
        return fail(f"input script not found: {input_path.relative_to(ROOT)}")
    # Interactive runs drive the game live (e.g. to sit at a @stop breakpoint), and
    # recording generates the transcript, so neither needs an existing baseline.
    comparing = not record and not interactive
    if comparing and not expected_path.is_file():
        return fail(f"transcript baseline not found: {expected_path.relative_to(ROOT)} (create it with RECORD=1)")

    scenario_id = scenario_path.relative_to(SCENARIOS).parent
    result_dir = results / "scenarios" / scenario_id
    actual_path = result_dir / "actual.txt"
    actual_frame = result_dir / "final.bmp"
    log_path = result_dir / ("trace.log" if trace else "run.log")
    result_dir.mkdir(parents=True, exist_ok=True)

    # Stage save files produced by another scenario into the working directory (the
    # tool runs with cwd=result_dir), so a later part can LOAD the earlier part's
    # saved position at boot. The source is the producer's recorded baseline, so this
    # scenario runs standalone without re-running the producer.
    for spec in as_list(scenario.get("consumes")):
        producer_id = spec.get("from")
        save_name = spec.get("file")
        if not producer_id or not save_name:
            return fail("invalid 'consumes' entry (needs 'from' and 'file')")
        saved = SCENARIOS / producer_id / save_name
        if not saved.is_file():
            return fail(f"required save file not found: {saved.relative_to(ROOT)} (record {producer_id} with RECORD=1)")
        shutil.copyfile(saved, result_dir / save_name)

    generated_input = result_dir / "generated.input"
    capture_paths: dict[str, Path] = {}
    capture_names: dict[str, str] = {}
    input_lines: list[str] = []
    capture_index = 0

    part = scenario.get("part")
    part_capture: Path | None = None
    if part is not None:
        if not isinstance(part, int) or part < 1:
            print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: invalid part '{part}'")
            return False
        part_capture = result_dir / "captures" / "part-selector.bmp"
        part_capture.parent.mkdir(parents=True, exist_ok=True)
    for line in input_path.read_text(encoding="utf-8").splitlines(keepends=True):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped in ("@interactive", "@stop") and not interactive:
            print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: {stripped} requires --interactive")
            return False
        if stripped.startswith("@capture ") or stripped.startswith("@save "):
            capture_name = stripped.split(maxsplit=1)[1].strip()
            capture_baseline = Path(capture_name)
            if not capture_name or capture_baseline.is_absolute() or ".." in capture_baseline.parts:
                print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: invalid capture '{capture_name}'")
                return False
            if capture_baseline.suffix == "":
                capture_baseline = capture_baseline.with_suffix(".png")
            capture_baseline = capture_baseline.with_name(f"{capture_index:02d}-{capture_baseline.name}")
            capture_id = capture_baseline.as_posix()
            capture_path = result_dir / "captures" / capture_baseline.with_suffix(".bmp")
            capture_path.parent.mkdir(parents=True, exist_ok=True)
            capture_paths[capture_id] = capture_path
            capture_names[capture_id] = capture_baseline.as_posix()
            input_lines.append(f"@capture {capture_path}\n")
            capture_index += 1
        else:
            input_lines.append(line)
    if part_capture is not None:
        # Registered after the scripted captures so the numbered baselines are
        # verified first; the part selector is a separate startup checkpoint.
        capture_paths["part-selector"] = part_capture
        capture_names["part-selector"] = "part-selector.png"
    generated_input.write_text("".join(input_lines), encoding="utf-8")
    command = [str(ddb), "-o", str(actual_path), "-i", str(generated_input)]
    if trace:
        command.insert(1, "-v")
    if interactive:
        command.insert(1, "--interactive")
    if skip_timed_pauses:
        command.insert(1, "--skip-timed-pauses")
    if part is not None:
        command.extend(("--part", str(part), "--part-capture", str(part_capture)))
    if scenario.get("screen"):
        command.extend(("--screen", str(scenario["screen"])))
    expected_frame = scenario.get("screenshot")
    if expected_frame:
        command.extend(("--capture", str(actual_frame)))
    command.append("test")
    command.extend(str(f) for f in fixtures)
    environment = os.environ.copy()
    if interactive:
        environment.pop("SDL_VIDEODRIVER", None)
        environment.pop("SDL_AUDIODRIVER", None)
    else:
        environment.setdefault("SDL_VIDEODRIVER", "dummy")
        environment.setdefault("SDL_AUDIODRIVER", "dummy")
    live_trace = interactive and trace
    try:
        # Run from result_dir so any files the player writes to its working
        # directory (e.g. a PART1.cfg persisting the video mode) land under the
        # gitignored test-results tree instead of polluting the repo. All command
        # paths are absolute, so the working directory is otherwise irrelevant.
        if live_trace:
            with log_path.open("wb") as trace_file:
                completed = subprocess.run(
                    command, cwd=result_dir, stdout=trace_file,
                    stderr=subprocess.STDOUT, timeout=None, env=environment,
                )
        else:
            completed = subprocess.run(
                command, cwd=result_dir, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, timeout=int(scenario.get("timeout_seconds", 30)), env=environment,
            )
    except subprocess.TimeoutExpired as error:
        output = decode_tool_output(error.stdout) or "scenario timed out\n"
        diagnostics = [
            f"scenario timed out after {scenario.get('timeout_seconds', 30)} seconds",
            f"command: {' '.join(command)}",
            f"generated input: {generated_input.relative_to(ROOT)}",
        ]
        if actual_path.is_file():
            diagnostics.append(f"partial transcript: {actual_path.relative_to(ROOT)} ({actual_path.stat().st_size} bytes)")
        log_path.write_text("\n".join(diagnostics) + "\n\n" + output, encoding="utf-8")
        location = actual_path.relative_to(ROOT) if actual_path.is_file() else log_path.relative_to(ROOT)
        print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: timed out (see {location})")
        return False
    if not live_trace:
        log_path.write_text(decode_tool_output(completed.stdout), encoding="utf-8")
    if completed.returncode != 0 or not actual_path.is_file():
        print(f"FAIL scenario {scenario_path.relative_to(ROOT)} (see {log_path.relative_to(ROOT)})")
        return False

    actual = normalize_transcript(actual_path.read_text(encoding="utf-8"))
    if record:
        local_transcript = base / scenario["transcript"]
        shared_transcript = shared_base / scenario["transcript"] if shared_base else None
        if shared_transcript is not None and shared_transcript.is_file() and \
                normalize_transcript(shared_transcript.read_text(encoding="utf-8")) == actual:
            # Identical to the shared transcript: keep it shared, drop any local copy.
            if local_transcript.exists():
                local_transcript.unlink()
        else:
            local_transcript.write_text(actual, encoding="utf-8")
    elif comparing:
        expected = normalize_transcript(expected_path.read_text(encoding="utf-8"))
        if actual != expected:
            diff_path = result_dir / "expected.txt"
            diff_path.write_text(expected, encoding="utf-8")
            print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: transcript differs")
            return False
    if expected_frame:
        capture_names["final"] = expected_frame
        capture_paths["final"] = actual_frame
    written_baselines: set[Path] = set()
    if capture_names and (record or comparing):
        try:
            from PIL import Image, ImageChops
        except ImportError:
            print("FAIL scenario screenshot comparison requires Pillow", file=sys.stderr)
            return False

        def load_rgb(path: Path):
            with Image.open(path) as image:
                return image.convert("RGB")

        def images_differ(left: Path, right: Path) -> bool:
            # Compare in RGB, not RGBA: screenshots are opaque, so an RGBA diff has a
            # uniformly-zero alpha channel and getbbox() would trim the whole image
            # away and report everything identical.
            lr, rr = load_rgb(left), load_rgb(right)
            return lr.size != rr.size or ImageChops.difference(lr, rr).getbbox() is not None

        for capture_id, name in capture_names.items():
            actual_frame_path = capture_paths[capture_id]
            if not actual_frame_path.is_file():
                print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: missing {capture_id} screenshot")
                return False
            if record:
                local_target = base / name
                shared_target = shared_base / name if shared_base else None
                if shared_target is not None and shared_target.is_file() and not images_differ(actual_frame_path, shared_target):
                    # Identical to the shared baseline: keep it shared, drop any local copy.
                    if local_target.exists():
                        local_target.unlink()
                    continue
                local_target.parent.mkdir(parents=True, exist_ok=True)
                with Image.open(actual_frame_path) as actual_image:
                    actual_image.convert("RGBA").save(local_target)
                written_baselines.add(local_target.resolve())
                continue
            expected_frame_path = baseline(name)
            if not expected_frame_path.is_file():
                print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: missing {capture_id} screenshot baseline")
                return False
            if images_differ(expected_frame_path, actual_frame_path):
                er, ar = load_rgb(expected_frame_path), load_rgb(actual_frame_path)
                if er.size == ar.size:
                    diff_path = result_dir / "captures" / Path(capture_id).with_suffix(".diff.png")
                    diff_path.parent.mkdir(parents=True, exist_ok=True)
                    ImageChops.difference(er, ar).save(diff_path)
                print(f"FAIL scenario {scenario_path.relative_to(ROOT)}: {capture_id} screenshot differs")
                return False
    if record:
        stale_screenshots = [path for path in base.glob("*.png") if path.resolve() not in written_baselines]
        for path in stale_screenshots:
            path.unlink()
        if stale_screenshots:
            print(f"Removed {len(stale_screenshots)} stale screenshot(s) from {base.relative_to(ROOT)}")

    # Save files the game wrote (multi-part handoff): record/compare them as byte
    # baselines beside the scenario, so the part-boundary game state is checked and
    # a later part can consume it.
    for name in as_list(scenario.get("produces")):
        produced = result_dir / name
        if not produced.is_file():
            return fail(f"expected produced file not written: {name}")
        # The save is a per-scenario artifact (its bytes can differ by platform), so
        # it is kept local — not deduped against the shared base like screenshots.
        baseline_file = base / name
        if record:
            baseline_file.write_bytes(produced.read_bytes())
        elif comparing:
            if not baseline_file.is_file():
                return fail(f"produced-file baseline not found: {name} (create it with RECORD=1)")
            if produced.read_bytes() != baseline_file.read_bytes():
                return fail(f"produced file differs: {name}")

    print(f"PASS scenario {scenario_path.relative_to(ROOT)}")
    return True


def run_host(ddb: Path, games: set[str], inventory: bool, scenario: Path | None, record: bool, trace: bool, interactive: bool, skip_timed_pauses: bool) -> int:
    manifest = load_manifest()
    if manifest.get("format") != 1:
        print("Unsupported fixture manifest format", file=sys.stderr)
        return 2
    if scenario is not None:
        return 0 if run_scenario(ddb, scenario, RESULTS, record, trace, interactive, skip_timed_pauses) else 1
    probes = discover_probes(manifest, games, inventory)
    if not probes:
        print("No fixture images selected", file=sys.stderr)
        return 2
    RESULTS.mkdir(exist_ok=True)
    failures = sum(not run_probe(ddb, probe, RESULTS) for probe in probes)
    if not inventory:
        scenarios = sorted(SCENARIOS.rglob("scenario.json"))
        if games:
            scenarios = [path for path in scenarios if path.relative_to(SCENARIOS).parts[0] in games]
        failures += sum(not run_scenario(ddb, scenario, RESULTS, trace=trace, interactive=interactive, skip_timed_pauses=skip_timed_pauses) for scenario in scenarios)
    label = "inventory" if inventory else "baseline"
    print(f"Probed {len(probes)} {label} fixture image(s): {len(probes) - failures} passed, {failures} failed")
    return 1 if failures else 0


def run_dos(games: set[str]) -> int:
    dosbox = shutil.which("dosbox-x")
    if dosbox is None:
        print("SKIP DOS suite: dosbox-x is not installed")
        return 0
    print("SKIP DOS suite: no DOS scenario fixtures have been added yet")
    print("Use the dedicated DOS test mode once a DOS-compatible scenario is present.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ddb", type=Path, required=True, help="host ddb tool")
    parser.add_argument("--suite", choices=("host", "dos"), default="host")
    parser.add_argument("--game", action="append", default=[], help="limit to a fixture game id")
    parser.add_argument("--inventory", action="store_true", help="probe every image instead of manifest baselines")
    parser.add_argument("--scenario", help="run one scenario, relative to tests/fixtures/scenarios")
    parser.add_argument("--record", action="store_true", help="write transcript and capture baselines for --scenario")
    parser.add_argument("--trace", action="store_true", help="enable interpreter tracing in scenario run logs")
    parser.add_argument("--interactive", action="store_true", help="show the game and hand scripted tests to live input")
    parser.add_argument("--skip-timed-pauses", action="store_true", help="skip finite PAUSE condacts in a scenario run")
    args = parser.parse_args()

    if args.suite == "dos":
        return run_dos(set(args.game))
    if not args.ddb.is_file():
        print(f"ddb tool not found: {args.ddb}", file=sys.stderr)
        return 2
    args.ddb = args.ddb.resolve()  # absolute so the tool can run from any cwd
    scenario = None
    if args.scenario:
        relative = Path(args.scenario)
        scenario = relative if relative.is_absolute() else SCENARIOS / relative
        if scenario.suffix != ".json":
            scenario = scenario / "scenario.json"
        if not scenario.is_file():
            print(f"Scenario not found: {scenario}", file=sys.stderr)
            return 2
    return run_host(args.ddb, set(args.game), args.inventory, scenario, args.record, args.trace, args.interactive, args.skip_timed_pauses)


if __name__ == "__main__":
    raise SystemExit(main())
