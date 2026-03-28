from pathlib import Path
import numpy as np
from PIL import Image
from common import load_image_float, abs_diff, luminance

RUN_DIR = Path("../runs/2026-03-28_cornell_skull_dragon")
REFERENCE = RUN_DIR / "reference" / "image.png"

METHODS = {
    "baseline_1spp": RUN_DIR / "baseline_1spp" / "image.png",
    "ta_1spp": RUN_DIR / "ta_1spp" / "image.png",
    "ta_denoise_1spp": RUN_DIR / "ta_denoise_1spp" / "image.png",
    "raw_16spp": RUN_DIR / "raw_16spp" / "image.png",
    "raw_64spp": RUN_DIR / "raw_64spp" / "image.png",
}

def save_rgb_image(arr: np.ndarray, path: Path):
    arr = np.clip(arr, 0.0, 1.0)
    img = Image.fromarray((arr * 255.0).astype(np.uint8))
    img.save(path)

def main():
    ref = load_image_float(str(REFERENCE))
    out_dir = RUN_DIR / "plots" / "heatmaps"
    out_dir.mkdir(parents=True, exist_ok=True)

    for name, path in METHODS.items():
        img = load_image_float(str(path))

        rgb_diff = abs_diff(img, ref)
        save_rgb_image(rgb_diff * 4.0, out_dir / f"{name}_rgb_absdiff.png")

        lum_diff = np.abs(luminance(img) - luminance(ref))
        lum_vis = np.stack([lum_diff, lum_diff, lum_diff], axis=-1)
        save_rgb_image(lum_vis * 8.0, out_dir / f"{name}_luminance_absdiff.png")

    print(f"Saved heatmaps to {out_dir}")

if __name__ == "__main__":
    main()