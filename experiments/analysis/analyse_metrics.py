from pathlib import Path
import csv
from common import load_image_float, mse, rmse, psnr

RUN_DIR = Path("../runs/2026-03-28_cornell_skull_dragon")
REFERENCE = RUN_DIR / "reference" / "image.png"

METHODS = {
    "baseline_1spp": RUN_DIR / "baseline_1spp" / "image.png",
    "ta_1spp": RUN_DIR / "ta_1spp" / "image.png",
    "ta_denoise_1spp": RUN_DIR / "ta_denoise_1spp" / "image.png",
    "raw_16spp": RUN_DIR / "raw_16spp" / "image.png",
    "raw_64spp": RUN_DIR / "raw_64spp" / "image.png",
}

def main():
    ref = load_image_float(str(REFERENCE))
    out_csv = RUN_DIR / "metrics" / "metrics.csv"
    out_csv.parent.mkdir(parents=True, exist_ok=True)

    rows = []
    for name, path in METHODS.items():
        img = load_image_float(str(path))
        rows.append({
            "method": name,
            "mse": mse(img, ref),
            "rmse": rmse(img, ref),
            "psnr": psnr(img, ref),
        })

    with open(out_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["method", "mse", "rmse", "psnr"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"Saved metrics to {out_csv}")
    for row in rows:
        print(row)

if __name__ == "__main__":
    main()