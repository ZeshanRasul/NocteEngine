import os
import numpy as np
import matplotlib.pyplot as plt
from PIL import Image


# ============================================================
# USER SETTINGS: CHANGE THESE PATHS
# ============================================================

BASELINE_PATH = r"./Caustics/Caustics-Transfer-To-Diffuse/Baseline_4096SPP.png"
RL_PATH       = r"./Caustics/Caustics-Transfer-To-Diffuse/RLPT_4096SPP.png"
GT_PATH       = r"./Caustics/Caustics-Transfer-To-Diffuse/GT_4096SPP.png"

OUTPUT_DIR    = r"./Caustics/Caustics-Transfer-To-Diffuse/charts"
OUTPUT_NAME   = "rl_vs_baseline_figure_4096SPP.png"

# Equal-cost label for the figure/caption
SPP_LABEL = "4096 spp"
GT_LABEL  = "4096 spp"


# Improvement visualization clamp
IMPROVEMENT_ABS_VMAX = 0.10

# Crop margins if needed: (left, top, right, bottom)
# Set to None to disable cropping
CROP_BOX = None
# Example:
# CROP_BOX = (0, 0, 0, 0)


# ============================================================
# IMAGE LOADING / PREP
# ============================================================

def load_image_float(path: str) -> np.ndarray:
    img = Image.open(path).convert("RGB")
    arr = np.asarray(img).astype(np.float32) / 255.0
    return arr


def crop_image(img: np.ndarray, crop_box):
    if crop_box is None:
        return img
    left, top, right, bottom = crop_box
    h, w = img.shape[:2]
    return img[top:h-bottom, left:w-right]


def match_shape(images):
    min_h = min(img.shape[0] for img in images)
    min_w = min(img.shape[1] for img in images)
    cropped = [img[:min_h, :min_w] for img in images]
    return cropped


def compute_mse(img_a: np.ndarray, img_b: np.ndarray) -> float:
    return float(np.mean((img_a - img_b) ** 2))


def compute_psnr(img_a: np.ndarray, img_b: np.ndarray, max_val: float = 1.0) -> float:
    mse = compute_mse(img_a, img_b)
    if mse <= 1e-12:
        return float("inf")
    return float(10.0 * np.log10((max_val ** 2) / mse))


def luminance(img: np.ndarray) -> np.ndarray:
    return 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]


# ============================================================
# MAIN
# ============================================================

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    baseline = load_image_float(BASELINE_PATH)
    rl = load_image_float(RL_PATH)
    gt = load_image_float(GT_PATH)

    baseline = crop_image(baseline, CROP_BOX)
    rl = crop_image(rl, CROP_BOX)
    gt = crop_image(gt, CROP_BOX)

    baseline, rl, gt = match_shape([baseline, rl, gt])

    # --------------------------------------------------------
    # Error maps
    # --------------------------------------------------------
    # Per-pixel RGB absolute error reduced to luminance-like scalar
    baseline_err_rgb = np.abs(baseline - gt)
    rl_err_rgb = np.abs(rl - gt)

    baseline_err = np.mean(baseline_err_rgb, axis=2)
    rl_err = np.mean(rl_err_rgb, axis=2)

    ERROR_VMAX = np.percentile(baseline_err, 99)
    # Positive means RL better
    improvement = baseline_err - rl_err
# Error visualization clamp

    # --------------------------------------------------------
    # Metrics
    # --------------------------------------------------------
    baseline_mse = compute_mse(baseline, gt)
    rl_mse = compute_mse(rl, gt)

    baseline_psnr = compute_psnr(baseline, gt)
    rl_psnr = compute_psnr(rl, gt)

    mse_improvement_pct = 100.0 * (baseline_mse - rl_mse) / max(baseline_mse, 1e-12)
    psnr_gain = rl_psnr - baseline_psnr

    print("Baseline MSE :", baseline_mse)
    print("RL MSE       :", rl_mse)
    print("Baseline PSNR:", baseline_psnr)
    print("RL PSNR      :", rl_psnr)
    print("MSE reduction (%):", mse_improvement_pct)
    print("PSNR gain (dB)   :", psnr_gain)

    # --------------------------------------------------------
    # Figure
    # --------------------------------------------------------
    fig, axes = plt.subplots(2, 3, figsize=(16, 10))
    fig.patch.set_facecolor("white")

    # Top row
    axes[0, 0].imshow(np.clip(baseline, 0.0, 1.0))
    axes[0, 0].set_title(f"Baseline ({SPP_LABEL})", fontsize=14)

    axes[0, 1].imshow(np.clip(rl, 0.0, 1.0))
    axes[0, 1].set_title(f"RL ({SPP_LABEL})", fontsize=14)

    axes[0, 2].imshow(np.clip(gt, 0.0, 1.0))
    axes[0, 2].set_title(f"Ground Truth ({GT_LABEL})", fontsize=14)

    # Bottom row
    im0 = axes[1, 0].imshow(
        baseline_err,
        cmap="inferno",
        vmin=0.0,
        vmax=ERROR_VMAX
    )
    axes[1, 0].set_title("Absolute Error: Baseline vs GT", fontsize=14)

    im1 = axes[1, 1].imshow(
        rl_err,
        cmap="inferno",
        vmin=0.0,
        vmax=ERROR_VMAX
    )
    axes[1, 1].set_title("Absolute Error: RL vs GT", fontsize=14)

    im2 = axes[1, 2].imshow(
        improvement,
        cmap="bwr",
        vmin=-IMPROVEMENT_ABS_VMAX,
        vmax=IMPROVEMENT_ABS_VMAX
    )
    axes[1, 2].set_title("Improvement Map (positive = RL better)", fontsize=14)

    # Remove axes
    for ax in axes.flat:
        ax.set_xticks([])
        ax.set_yticks([])

    # Colorbars
    cbar0 = fig.colorbar(im0, ax=axes[1, 0], fraction=0.046, pad=0.04)
    cbar0.set_label("Absolute Error", fontsize=11)

    cbar1 = fig.colorbar(im1, ax=axes[1, 1], fraction=0.046, pad=0.04)
    cbar1.set_label("Absolute Error", fontsize=11)

    cbar2 = fig.colorbar(im2, ax=axes[1, 2], fraction=0.046, pad=0.04)
    cbar2.set_label("Error Difference", fontsize=11)

    # Metrics text
    metrics_text = (
        f"Baseline MSE: {baseline_mse:.6f}\n"
        f"RL MSE: {rl_mse:.6f}\n"
        f"MSE reduction: {mse_improvement_pct:.2f}%\n"
        f"Baseline PSNR: {baseline_psnr:.3f} dB\n"
        f"RL PSNR: {rl_psnr:.3f} dB\n"
        f"PSNR gain: {psnr_gain:.3f} dB"
    )

    fig.text(
        0.5,
        0.02,
        metrics_text,
        ha="center",
        va="bottom",
        fontsize=12,
        bbox=dict(boxstyle="round", facecolor="white", alpha=0.9, edgecolor="gray")
    )

    plt.tight_layout(rect=[0, 0.08, 1, 1])

    output_path = os.path.join(OUTPUT_DIR, OUTPUT_NAME)
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    plt.show()

    print(f"Saved figure to: {output_path}")


if __name__ == "__main__":
    main()