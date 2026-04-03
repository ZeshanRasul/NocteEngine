import numpy as np
import imageio as imageio

def load_img(path):
    img = imageio.imread(path).astype(np.float32) / 255.0
    return img

def mse(a, b):
    return np.mean((a - b) ** 2)

def psnr(a, b):
    return -10 * np.log10(mse(a, b))

gt = load_img("./RL_Eval/gt_4096SPP.png")
baseline = load_img("./RL_Eval/Baseline_Cornell_Box_Glass_4096SPP.png")
rl = load_img("./RL_Eval/RL_Eval_4096SPP.png")

print("Baseline MSE:", mse(baseline, gt))
print("RL MSE:", mse(rl, gt))

print("Baseline PSNR:", psnr(baseline, gt))
print("RL PSNR:", psnr(rl, gt))