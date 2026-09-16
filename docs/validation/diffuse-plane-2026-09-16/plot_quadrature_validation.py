"""Generate a bounded validation figure and table from a fixed eight-run batch.

Requires matplotlib. Example:
From this script's directory:
python plot_quadrature_validation.py --root runs --batch 2026-09-16_12-34-53Diffuse_Plane --out .
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from compare_quadrature_runs import read_run, summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--batch', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    runs, seeds, sources = [], [], []
    for i in range(1, 9):
        path = args.root / f'{args.batch}_{i}' / 'quadrature_integral.json'
        run, seed = read_run(path, 256)
        runs.append(run)
        seeds.append(seed)
        sources.append(dict(path=Path(os.path.relpath(path.resolve(), args.out.resolve())).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), base_seed=seed))
    if None in seeds or len(set(seeds)) != 8:
        raise ValueError('Eight distinct recorded seeds required')
    rows = summarize(runs)
    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / 'statistics.csv').open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    (args.out / 'sources.json').write_text(json.dumps(sources, indent=2), encoding='utf-8')

    probes = [(970, 715, 'Centre', '#2463a0'), (1125, 718, 'Right edge', '#c65b22'), (810, 700, 'Left edge', '#168273')]
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.7))
    summary = []
    for x, y, label, color in probes:
        selected = sorted([r for r in rows if r['x']==x and r['y']==y and r['channel']=='R'], key=lambda r:r['spp'])
        ns = [r['spp'] for r in selected]
        axes[0].loglog(ns, [r['rmse_percent'] for r in selected], 'o-', color=color, label=label, linewidth=2)
        last = selected[-1]
        summary.append((label, last))
    # A slope guide, not a fitted model or acceptance threshold.
    axes[0].loglog([256, 4096], [0.2, 0.05], '--', color='#777777', label=r'$N^{-1/2}$ guide')
    axes[0].set(xlabel='Samples per pixel', ylabel='RMS relative error (%)', title='Error across eight seeded runs')
    axes[0].set_xticks([1, 256, 1024, 4096], ['1', '256', '1,024', '4,096'])
    axes[0].grid(True, which='major', alpha=.2)
    axes[0].legend(fontsize=9)
    for i, ((label, r), (_, _, _, color)) in enumerate(zip(summary, probes)):
        # Two standard errors are descriptive uncertainty bars, not exact 95% CIs.
        sem = r['stddev_percent'] / math.sqrt(8)
        axes[1].errorbar(i, r['mean_signed_percent'], yerr=2*sem, fmt='o', capsize=6, color=color, markersize=7)
    axes[1].axhline(0, color='#777777', linestyle='--', linewidth=1)
    axes[1].set_xticks(range(3), [p[2] for p in probes])
    axes[1].set(ylabel='Mean signed relative error (%)', title='Residual offset at 4,096 SPP')
    axes[1].grid(True, axis='y', alpha=.2)
    fig.suptitle('NocteEngine: diffuse-plane direct illumination', fontsize=15)
    fig.text(.5, .015, 'Red channel | 256 × 256 CPU midpoint quadrature | Right: mean ± 2 standard errors (not exact confidence intervals)', ha='center', fontsize=9)
    fig.tight_layout(rect=(0,.05,1,.94))
    for ext in ('png','svg','pdf'):
        fig.savefig(args.out / f'convergence.{ext}', dpi=180)
    plt.close(fig)
    lines = ['# Diffuse-plane quadrature validation', '',
             f'Batch: `{args.batch}`. Eight separately seeded runs; seeds {seeds}.', '',
             '![Convergence figure](convergence.png)', '',
             'All table values below are percentages, using the red channel at 4,096 SPP. CSV includes every RGB channel and checkpoint.', '',
             '| Probe | Mean signed error | Sample SD | Standard error of mean | RMS error |',
             '|---|---:|---:|---:|---:|']
    for label, r in summary:
        lines.append(f"| {label} | {r['mean_signed_percent']:+.5f} | {r['stddev_percent']:.5f} | {r['stddev_percent']/math.sqrt(8):.5f} | {r['rmse_percent']:.5f} |")
    lines += ['', '## What this supports', '',
              'For this fixed, unobstructed diffuse-plane fixture, three rendered probes approach a separately evaluated double-precision rectangle integral as sample count increases. RMS error decreases at every recorded checkpoint for all three red-channel probes. At 4,096 SPP, each mean signed red-channel error is within two estimated standard errors of zero; eight runs do not establish absence of small bias.', '',
              'The left panel uses sqrt(mean((render/reference - 1)^2)) across runs. The right panel uses the sample standard deviation divided by sqrt(8). Error bars are descriptive ±2 SEM, not exact 95% confidence intervals. Checkpoints within each run share samples. Channels share samples and are not independent trials.', '',
              '## Scope and remaining gates', '',
              '- Reference: 256-by-256 midpoint quadrature at each recorded world point, not an exact closed-form solution. Refinement was checked in the supplied captures.',
              '- Pointwise comparison assumes fixed pixel-centre primary rays, zero aperture, matching world intersections, and no occluder. Full-image pixel-filter agreement is a separate test.',
              '- Captures and source inspection support the corrected upload/reset/capture sequence for one sample per frame. GPU constants were not independently read back by this plotting script.',
              '- Verify multiple samples per frame and sample-key invariance across frame grouping before closing that gate.',
              '- Confirm bounded CLI replay, complete configuration/provenance, build/shader/scene hashes, hardware/driver records and validation-output retention.',
              '- Complete GPU pass timing/PIX annotations and separate CPU update, recording, wait and present measurements. These synchronous readback runs are correctness evidence, not representative performance measurements.',
              '- Add an independent Mitsuba RGB scene comparison with matching geometry, light units, reflectance, camera and pixel-filter conventions. Track point versus pixel-average differences explicitly.',
              '- This does not validate general materials, visibility/shadows, indirect transport, volumes, temporal reconstruction, arbitrary scenes or performance.', '',
              'The source manifest hashes the input JSON files only; it does not substitute for missing renderer build/shader/scene provenance.', '',
              '## Reproduction', '',
              'Paths in sources.json are relative to its containing directory. From this report directory, run:', '',
              '```powershell',
              'python compare_quadrature_runs.py "runs/*/quadrature_integral.json" --csv statistics.csv',
              'python plot_quadrature_validation.py --root runs --batch 2026-09-16_12-34-53Diffuse_Plane --out .',
              '```', '',
              'Plotting requires Matplotlib; the comparison script uses only the Python standard library. Preserve the source JSONs with this report. CSV is globally ignored by the repository, so explicitly choose how validation artifacts are retained.']
    (args.out / 'REPORT.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print(args.out.resolve())


if __name__ == '__main__':
    main()
