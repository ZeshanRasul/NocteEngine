#!/usr/bin/env python3
"""Compare two renders numerically.

Turns "the arch looks different" into a number. Prints RMSE (overall and
per-channel), PSNR and mean-brightness ratio, and optionally writes an amplified
difference heatmap so the spatial distribution of the error is visible.

Typical use -- an RIS on/off pair captured from one locked viewpoint:

    python tools/compare_renders.py a_RIS1024SPP.png b_NoRIS1024SPP.png --heatmap diff.png

Converged, an unbiased pair must agree: mean-ratio near 1.000 and a low RMSE.
A persistent brightness ratio away from 1.0 indicates bias, not variance.

Against a ground-truth reference, RMSE at matched sample counts is the number
that belongs on a convergence curve.

Requires Pillow. numpy is used when present (much faster) but is not required.
"""

import argparse
import math
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  python -m pip install pillow")

try:
    import numpy as np
except ImportError:
    np = None


def load_rgb(path):
    try:
        img = Image.open(path)
    except OSError as exc:
        sys.exit(f"cannot open {path}: {exc}")
    # Captures are RGBA; alpha is not part of the comparison.
    return img.convert("RGB")


def stats_numpy(a, b):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    diff = a - b
    per_channel = [math.sqrt(float((diff[:, :, c] ** 2).mean())) for c in range(3)]
    rmse = math.sqrt(float((diff ** 2).mean()))
    return rmse, per_channel, float(a.mean()), float(b.mean())


def stats_pillow(a, b):
    from PIL import ImageChops, ImageStat

    # ImageStat computes in C; rms is per band.
    diff = ImageChops.difference(a, b)
    st = ImageStat.Stat(diff)
    per_channel = list(st.rms)
    # Overall RMSE across all channels, weighted equally.
    rmse = math.sqrt(sum(v * v for v in per_channel) / len(per_channel))
    return rmse, per_channel, sum(ImageStat.Stat(a).mean) / 3.0, sum(ImageStat.Stat(b).mean) / 3.0


def write_heatmap(a, b, path, gain):
    """Blue (no difference) -> red (large difference), amplified by `gain`.

    Implemented with Pillow point-LUTs so it produces identical output with or
    without numpy -- an earlier version silently fell back to plain greyscale.
    """
    from PIL import ImageChops

    diff = ImageChops.difference(a, b).convert("L")

    # Report the actual error range so the gain can be judged rather than guessed.
    lo, hi = diff.getextrema()
    hist = diff.histogram()
    total = sum(hist)
    running, p999 = 0, hi
    for value, count in enumerate(hist):
        running += count
        if running >= total * 0.999:
            p999 = value
            break
    print(f"difference range : max {hi}  99.9th pct {p999}  (0-255 scale)")
    if p999 > 0:
        print(f"  suggested gain : x{max(1.0, 255.0 / max(p999, 1)):.0f}")

    amp = diff.point(lambda v: min(255, int(v * gain)))
    red = amp.point(lambda v: v)
    grn = amp.point(lambda v: min(v, 255 - v))
    blu = amp.point(lambda v: 255 - v)
    Image.merge("RGB", (red, grn, blu)).save(path)

    print(f"heatmap written  : {path}  (gain x{gain:g})")
    print("  deep blue = identical, cyan/green = small, red = large difference")


def main():
    ap = argparse.ArgumentParser(description="Compare two renders (RMSE / PSNR / brightness).")
    ap.add_argument("image_a")
    ap.add_argument("image_b")
    ap.add_argument("--heatmap", metavar="PATH", help="write an amplified difference image")
    ap.add_argument("--gain", type=float, default=8.0, help="heatmap amplification (default 8)")
    args = ap.parse_args()

    a = load_rgb(args.image_a)
    b = load_rgb(args.image_b)

    if a.size != b.size:
        sys.exit(f"size mismatch: {a.size} vs {b.size} -- captures must share a viewpoint and resolution")

    rmse, per_channel, mean_a, mean_b = (stats_numpy if np is not None else stats_pillow)(a, b)

    psnr = float("inf") if rmse == 0 else 20.0 * math.log10(255.0 / rmse)
    ratio = (mean_b / mean_a) if mean_a > 0 else float("nan")

    print(f"A: {args.image_a}")
    print(f"B: {args.image_b}")
    print(f"resolution      : {a.size[0]}x{a.size[1]}")
    print(f"RMSE            : {rmse:.4f}  (0-255 scale)")
    print(f"  per channel   : R {per_channel[0]:.4f}  G {per_channel[1]:.4f}  B {per_channel[2]:.4f}")
    print(f"PSNR            : {psnr:.2f} dB")
    print(f"mean brightness : A {mean_a:.3f}   B {mean_b:.3f}   B/A {ratio:.4f}")

    # Interpretation aid. A converged unbiased pair differs in noise, not in mean.
    if abs(ratio - 1.0) > 0.02:
        print("\n  NOTE: mean brightness differs by more than 2%. For two converged")
        print("  unbiased estimators this indicates bias, not variance -- worth")
        print("  investigating before using the pair as evidence.")

    if args.heatmap:
        write_heatmap(a, b, args.heatmap, args.gain)


if __name__ == "__main__":
    main()
