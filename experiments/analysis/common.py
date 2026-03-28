from pathlib import Path
import numpy as np
from PIL import Image

def load_image_float(path: str) -> np.ndarray:
    img = Image.open(path).convert("RGB")
    arr = np.asarray(img).astype(np.float32) / 255.0
    return arr

def mse(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.mean((a - b) ** 2))

def rmse(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.sqrt(mse(a, b)))

def psnr(a: np.ndarray, b: np.ndarray, max_val: float = 1.0) -> float:
    m = mse(a, b)
    if m <= 1e-12:
        return float("inf")
    return float(10.0 * np.log10((max_val * max_val) / m))

def abs_diff(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.abs(a - b)

def luminance(img: np.ndarray) -> np.ndarray:
    return 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]