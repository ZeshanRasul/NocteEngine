"""Compare independent NocteEngine quadrature captures (Python 3.10+, stdlib only).

One input JSON = one run. Select a single quadrature resolution (default 256).
Usage: python tools/compare_quadrature_runs.py "captures/validation/*/quadrature_integral.json" --csv comparison.csv
Reported SPP is checked for internal consistency, not verified against GPU work.
Record a top-level base_seed in each capture and keep all scene/camera settings fixed.
"""

import argparse
import csv
import glob
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys


def rgb(value):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError("Expected an RGB array with three components")
    result = tuple(float(v) for v in value)
    if not all(math.isfinite(v) for v in result):
        raise ValueError("Non-finite RGB value")
    return result


def read_run(path, resolution):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    samples = {}
    pattern = re.compile(r"Integral Result(\d+)_(\d+)_(\d+)_(\d+)")
    for key, value in data.items():
        match = pattern.fullmatch(key)
        if not match:
            continue
        x, y, q, spp = map(int, match.groups())
        if q != resolution:
            continue
        suffix = f"{x}_{y}_{q}_{spp}"
        if spp <= 0 or data.get("Accumulated SPP" + suffix) != spp:
            raise ValueError(f"{path}: inconsistent/missing accumulated SPP for {suffix}")
        position = rgb(data[f"probe{x}_{y}_{spp}"]["world_position"])
        reference = rgb(value)
        rendered = rgb(data["Render Pixel Value" + suffix])
        samples[x, y, spp] = (reference, rendered, position)
    if not samples:
        raise ValueError(f"{path}: no captures at quadrature resolution {resolution}")
    # Detect reference/position changes even within a single file.
    probes = {}
    for (x, y, _), (ref, _, pos) in samples.items():
        if (x, y) in probes and probes[x, y] != (ref, pos):
            raise ValueError(f"{path}: reference/position changed across SPP for {(x, y)}")
        probes[x, y] = (ref, pos)
    return samples, data.get("base_seed")


def summarize(runs):
    keys = set(runs[0])
    for run in runs[1:]:
        if set(run) != keys:
            raise ValueError("Runs have different probe/SPP sets; use matching captures")
        for key in keys:
            # Strict equality is intentional for this fixed deterministic fixture.
            if run[key][0] != runs[0][key][0] or run[key][2] != runs[0][key][2]:
                raise ValueError(f"Reference or world position differs between runs at {key}")
    rows = []
    for x, y, spp in sorted(keys):
        for c, channel in enumerate("RGB"):
            ref = runs[0][x, y, spp][0][c]
            errors = [run[x, y, spp][1][c] - ref for run in runs]
            n = len(errors)
            mean = statistics.mean(errors)
            sd = statistics.stdev(errors) if n > 1 else None
            rmse = math.sqrt(statistics.mean(e * e for e in errors))
            scale = 100 / abs(ref) if ref != 0 else None
            rows.append(dict(
                x=x, y=y, spp=spp, channel=channel, runs=n, reference=ref,
                mean_signed_error=mean, sample_stddev=sd,
                standard_error_mean=sd / math.sqrt(n) if sd is not None else None,
                rmse=rmse,
                mean_signed_percent=mean * scale if scale is not None else None,
                stddev_percent=sd * scale if sd is not None and scale is not None else None,
                rmse_percent=rmse * scale if scale is not None else None,
            ))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="JSON paths or quoted glob patterns")
    parser.add_argument("--resolution", type=int, default=256)
    parser.add_argument("--csv", type=Path, help="Write all RGB statistics to CSV")
    args = parser.parse_args()
    paths = []
    for item in args.inputs:
        matches = [Path(item)] if Path(item).is_file() else [Path(p) for p in glob.glob(item, recursive=True)]
        if not matches:
            raise ValueError(f"No files match {item!r}")
        paths.extend(p.resolve() for p in matches)
    if len(set(paths)) != len(paths):
        raise ValueError("Input patterns include the same file more than once")
    if args.csv and args.csv.resolve() in paths:
        raise ValueError("CSV output cannot overwrite an input")
    runs, seeds, digests = [], [], set()
    for path in sorted(paths):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest in digests:
            raise ValueError(f"Duplicate file contents: {path}; copied files are not independent runs")
        digests.add(digest)
        run, seed = read_run(path, args.resolution)
        runs.append(run)
        seeds.append(seed)
    rows = summarize(runs)
    if any(seed is None for seed in seeds):
        print("WARNING: base_seed missing; independence cannot be verified. Resetting the same sequence is not a new run.", file=sys.stderr)
    known_seeds = [json.dumps(s, sort_keys=True) for s in seeds if s is not None]
    if len(set(known_seeds)) != len(known_seeds):
        raise ValueError("Repeated base_seed: use distinct seeds for independent runs")
    if len(runs) == 1:
        print("WARNING: One run only. Standard deviation/standard error are unavailable; RMSE is absolute error.", file=sys.stderr)
    print(f"Runs: {len(runs)} | Quadrature: {args.resolution} | Signed error = render - reference")
    print("Confirm identical scene/camera/material settings and independent seeds; matching probes alone cannot establish this.")
    print("pixel       spp ch   mean error       sample SD        SEM              RMSE          mean %       SD %     RMS %")
    def fmt(v, percent=False):
        return "n/a" if v is None else (f"{v:.5f}" if percent else f"{v:.7e}")
    for row in rows:
        print(f"{row['x']},{row['y']:<5} {row['spp']:5} {row['channel']} "
              + " ".join(f"{fmt(row[k]):>15}" for k in (
                  "mean_signed_error", "sample_stddev", "standard_error_mean", "rmse"))
              + " " + " ".join(f"{fmt(row[k], True):>9}" for k in (
                  "mean_signed_percent", "stddev_percent", "rmse_percent")))
    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f"CSV: {args.csv.resolve()}")
    print("No automatic pass/fail: inspect mean error relative to SEM and RMSE across SPP. Checkpoints within a run are correlated.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError) as exc:
        sys.exit(f"ERROR: {exc}")
