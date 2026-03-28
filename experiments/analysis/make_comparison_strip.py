from pathlib import Path
from PIL import Image, ImageDraw

RUN_DIR = Path("../runs/2026-03-28_cornell_skull_dragon")

IMAGES = [
    ("Reference", RUN_DIR / "reference" / "image.png"),
    ("1 spp", RUN_DIR / "baseline_1spp" / "image.png"),
    ("1 spp + TA", RUN_DIR / "ta_1spp" / "image.png"),
    ("1 spp + TA + Denoise", RUN_DIR / "ta_denoise_1spp" / "image.png"),
    ("64 spp", RUN_DIR / "raw_64spp" / "image.png"),
]

def main():
    loaded = [(label, Image.open(path).convert("RGB")) for label, path in IMAGES]
    w, h = loaded[0][1].size
    label_h = 32
    canvas = Image.new("RGB", (w * len(loaded), h + label_h), color=(255, 255, 255))
    draw = ImageDraw.Draw(canvas)

    for i, (label, img) in enumerate(loaded):
        x = i * w
        canvas.paste(img, (x, label_h))
        draw.text((x + 10, 8), label, fill=(0, 0, 0))

    out = RUN_DIR / "plots" / "comparison_strip.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out)
    print(f"Saved {out}")

if __name__ == "__main__":
    main()