# compare_rl_baseline_vs_gt_spyder.py
#
# Compare RL renders and baseline renders against GT at:
# 4, 8, 16, 32, 256 SPP
#
# Outputs:
# - metrics_per_image.csv
# - metrics_summary.csv
# - plots for MSE / PSNR / RMSE / MAE
# - optional comparison strips

from pathlib import Path
from typing import Optional, Tuple

import math
import numpy as np
import pandas as pd
from PIL import Image
import matplotlib.pyplot as plt

SKIMAGE_AVAILABLE = False


# =============================================================================
# USER SETTINGS
# =============================================================================

GT_DIR = Path(r"./GT_Renders_3")
BASELINE_DIR = Path(r"./Baseline_Renders_3")
RL_DIR = Path(r"./RL_Renders_4")
OUTPUT_DIR = Path(r"./Charts/Comparison_Results_4")

SCENES = [
    "",
]

SPPS = [4, 8, 16, 32, 64, 128, 256]

EXT = "SPP.png"

# Pattern for RL and baseline renders
# Example: "scene1_4.png"
IMAGE_PATTERN = "{scene}{spp}{ext}"

# Pattern for GT
# Example: "scene1.png"
GT_PATTERN = "{scene}{ext}"

SAVE_STRIPS = True

# Optional ROI crop: (x0, y0, x1, y1)
# Set to None for full-image metrics only.
ROI: Optional[Tuple[int, int, int, int]] = None
# ROI = (200, 200, 500, 500)


# =============================================================================
# IMPLEMENTATION
# =============================================================================

def load_image_rgb(path: Path) -> np.ndarray:
    if not path.exists():
        raise FileNotFoundError(f"Image not found: {path}")

    img = Image.open(path).convert("RGB")
    arr = np.asarray(img, dtype=np.float32) / 255.0
    return arr


def maybe_crop(img: np.ndarray, roi: Optional[Tuple[int, int, int, int]]) -> np.ndarray:
    if roi is None:
        return img
    x0, y0, x1, y1 = roi
    return img[y0:y1, x0:x1, :]


def mse(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.mean((a - b) ** 2))


def rmse(a: np.ndarray, b: np.ndarray) -> float:
    return math.sqrt(mse(a, b))


def mae(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.mean(np.abs(a - b)))


def psnr(a: np.ndarray, b: np.ndarray, data_range: float = 1.0) -> float:
    err = mse(a, b)
    if err <= 1e-12:
        return float("inf")
    return 10.0 * math.log10((data_range * data_range) / err)


def ssim_rgb(a: np.ndarray, b: np.ndarray) -> Optional[float]:
    if not SKIMAGE_AVAILABLE:
        return None
    val = ssim_fn(a, b, channel_axis=2, data_range=1.0)
    return float(val)


def save_strip(
    gt_img: np.ndarray,
    base_img: np.ndarray,
    rl_img: np.ndarray,
    out_path: Path,
    title: str,
) -> None:
    fig = plt.figure(figsize=(12, 4))

    for i, (img, name) in enumerate([
        (gt_img, "GT"),
        (base_img, "Baseline"),
        (rl_img, "RL"),
    ]):
        ax = fig.add_subplot(1, 3, i + 1)
        ax.imshow(np.clip(img, 0.0, 1.0))
        ax.set_title(name)
        ax.axis("off")

    fig.suptitle(title)
    plt.tight_layout()
    plt.savefig(out_path, dpi=200, bbox_inches="tight")
    plt.close(fig)


def plot_metric_curves(df: pd.DataFrame, out_dir: Path, metric: str) -> None:
    scenes = sorted(df["scene"].unique())

    for scene in scenes:
        sub = df[df["scene"] == scene].copy()

        plt.figure(figsize=(7, 5))
        for method in ["baseline", "rl"]:
            ss = sub[sub["method"] == method].sort_values("spp")
            plt.plot(ss["spp"], ss[metric], marker="o", label=method)

        plt.xscale("log", base=2)
        if metric.lower() in {"mse", "rmse", "mae"}:
            plt.yscale("log")

        plt.xlabel("SPP")
        plt.ylabel(metric.upper())
        plt.title(f"{metric.upper()} vs SPP - {scene}")
        plt.legend()
        plt.tight_layout()
        plt.savefig(out_dir / f"{metric}_{scene}.png", dpi=200)
        plt.close()

    grouped = (
        df.groupby(["method", "spp"], as_index=False)[metric]
        .mean()
        .sort_values(["method", "spp"])
    )

    plt.figure(figsize=(7, 5))
    for method in ["baseline", "rl"]:
        ss = grouped[grouped["method"] == method]
        plt.plot(ss["spp"], ss[metric], marker="o", label=method)

    plt.xscale("log", base=2)
    if metric.lower() in {"mse", "rmse", "mae"}:
        plt.yscale("log")

    plt.xlabel("SPP")
    plt.ylabel(metric.upper())
    plt.title(f"Average {metric.upper()} vs SPP")
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_dir / f"{metric}_average.png", dpi=200)
    plt.close()


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    rows = []

    for scene in SCENES:
        gt_path = GT_DIR / GT_PATTERN.format(scene=scene, ext=EXT)
        gt_img = load_image_rgb(gt_path)
        gt_eval = maybe_crop(gt_img, ROI)

        for spp in SPPS:
            baseline_path = BASELINE_DIR / IMAGE_PATTERN.format(
                scene=scene, spp=spp, ext=EXT
            )
            rl_path = RL_DIR / IMAGE_PATTERN.format(
                scene=scene, spp=spp, ext=EXT
            )

            baseline_img = load_image_rgb(baseline_path)
            rl_img = load_image_rgb(rl_path)

            if baseline_img.shape != gt_img.shape or rl_img.shape != gt_img.shape:
                raise ValueError(
                    f"Shape mismatch in scene '{scene}', spp={spp}. "
                    f"GT={gt_img.shape}, baseline={baseline_img.shape}, RL={rl_img.shape}"
                )

            baseline_eval = maybe_crop(baseline_img, ROI)
            rl_eval = maybe_crop(rl_img, ROI)

            for method_name, img_eval in [
                ("baseline", baseline_eval),
                ("rl", rl_eval),
            ]:
                row = {
                    "scene": scene,
                    "spp": spp,
                    "method": method_name,
                    "mse": mse(img_eval, gt_eval),
                    "rmse": rmse(img_eval, gt_eval),
                    "mae": mae(img_eval, gt_eval),
                    "psnr": psnr(img_eval, gt_eval),
                    "ssim": ssim_rgb(img_eval, gt_eval),
                }
                rows.append(row)

            if SAVE_STRIPS:
                strip_path = OUTPUT_DIR / f"strip_{scene}_{spp}.png"
                save_strip(
                    gt_img=gt_img,
                    base_img=baseline_img,
                    rl_img=rl_img,
                    out_path=strip_path,
                    title=f"{scene} - {spp} SPP"
                )

    df = pd.DataFrame(rows)
    df.to_csv(OUTPUT_DIR / "metrics_per_image.csv", index=False)

    summary = (
        df.groupby(["method", "spp"], as_index=False)
        .agg({
            "mse": "mean",
            "rmse": "mean",
            "mae": "mean",
            "psnr": "mean",
            "ssim": "mean",
        })
        .sort_values(["method", "spp"])
    )
    summary.to_csv(OUTPUT_DIR / "metrics_summary.csv", index=False)

    plot_metric_curves(df, OUTPUT_DIR, "mse")
    plot_metric_curves(df, OUTPUT_DIR, "psnr")
    plot_metric_curves(df, OUTPUT_DIR, "rmse")
    plot_metric_curves(df, OUTPUT_DIR, "mae")

    if df["ssim"].notna().any():
        plot_metric_curves(df.dropna(subset=["ssim"]), OUTPUT_DIR, "ssim")

    print()
    print(f"[DONE] Wrote per-image metrics to: {OUTPUT_DIR / 'metrics_per_image.csv'}")
    print(f"[DONE] Wrote summary metrics to:   {OUTPUT_DIR / 'metrics_summary.csv'}")
    print(f"[DONE] Plots written to:            {OUTPUT_DIR}")


if __name__ == "__main__":
    main()