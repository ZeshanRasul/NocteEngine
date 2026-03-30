import numpy as np
import imageio as iio
import matplotlib.pyplot as plt
import os


# ----------------------------
# Utility Functions
# ----------------------------

def load_img(path):
    img = iio.imread(path).astype(np.float32) / 255.0
    return img


def mse(a, b):
    return np.mean((a - b) ** 2)


def psnr(a, b):
    m = mse(a, b)
    return -10.0 * np.log10(m + 1e-8)


def save_heatmap(err, path):
    # Convert RGB error to scalar
    err_vis = np.linalg.norm(err, axis=2)

    plt.figure(figsize=(6, 5))
    plt.imshow(err_vis, cmap='inferno')
    plt.colorbar()
    plt.title("Error Heatmap")
    plt.axis('off')
    plt.tight_layout()
    plt.savefig(path, dpi=150)
    plt.close()


def save_strip(images, labels, path):
    # Normalize all images to same size (assumes same resolution)
    strip = np.concatenate(images, axis=1)

    # Save image
    iio.imwrite(path, (strip * 255).astype(np.uint8))

def save_square_comparison(images, titles, path):
    assert len(images) == 4, "This function expects exactly 4 images for a 2x2 grid."

    fig, axes = plt.subplots(2, 2, figsize=(6, 6))  # square figure

    # Flatten for easy iteration
    axes = axes.flatten()

    for i, ax in enumerate(axes):
        ax.imshow(images[i])
        ax.text(0.5, -0.05, titles[i],
        transform=ax.transAxes,
        ha='center', va='top', fontsize=9)        
        ax.axis("off")

    # Tight layout for paper-ready look
    plt.subplots_adjust(wspace=0.02, hspace=0.02)

    # Save high quality
    plt.savefig(path, dpi=300, bbox_inches='tight', pad_inches=0.02)
    plt.close(fig)

def save_row_comparison(images, titles, path):
    assert len(images) == 2, "This function expects exactly 4 images for a 2x2 grid."

    fig, axes = plt.subplots(1, 2, figsize=(6, 6))  # square figure

    # Flatten for easy iteration
    axes = axes.flatten()

    for i, ax in enumerate(axes):
        ax.imshow(images[i])
        ax.text(0.5, -0.05, titles[i],
        transform=ax.transAxes,
        ha='center', va='top', fontsize=9)        
        ax.axis("off")

    # Tight layout for paper-ready look
    plt.subplots_adjust(wspace=0.02, hspace=0.02)

    # Save high quality
    plt.savefig(path, dpi=300, bbox_inches='tight', pad_inches=0.02)
    plt.close(fig)

# ----------------------------
# Main Function
# ----------------------------

def main():
    # -------- Paths --------
    render_dir = "renders"
    output_dir = "outputs"

    os.makedirs(output_dir, exist_ok=True)

    # Input images
    paths = {
        "1spp": os.path.join(render_dir, "1SPP.png"),
        "16spp": os.path.join(render_dir, "16SPP.png"),
        "64spp": os.path.join(render_dir, "64SPP.png"),
        "ground_truth": os.path.join(render_dir, "4096SPP.png"),
    }

    # -------- Load images --------
    img_1 = load_img(paths["1spp"])
    img_16 = load_img(paths["16spp"])
    img_64 = load_img(paths["64spp"])
    img_gt = load_img(paths["ground_truth"])

    # -------- Compute error (denoised vs GT) --------
    err = np.abs(img_64 - img_gt)

    # -------- Metrics --------
    print("==== Metrics (64spp vs GT) ====")
    print(f"MSE:  {mse(img_64, img_gt):.6f}")
    print(f"PSNR: {psnr(img_64, img_gt):.2f} dB")

    # -------- Save heatmap --------
    heatmap_path = os.path.join(output_dir, "error_heatmap.png")
    save_heatmap(err, heatmap_path)
    print(f"Saved heatmap → {heatmap_path}")

    # -------- Save comparison strip --------
    strip_path = os.path.join(output_dir, "comparison_strip.png")
    save_strip(
        [img_1, img_16, img_64, img_gt],
        ["1 spp", "16 spp", "64 spp", "4096 spp"],
        strip_path
    )
    print(f"Saved comparison strip → {strip_path}")
    
    grid_path = os.path.join(output_dir, "comparison_grid.png")

    save_square_comparison(
        [img_1, img_16, img_64, img_gt],
        ["1 spp", "16 spp", "64 spp", "4096 spp"],
        grid_path
    )
    
    print(f"Saved comparison grid → {grid_path}")
    
    row1_path = os.path.join(output_dir, "comparison_row1.png")
    
    save_row_comparison(
        [img_1, img_16],
        ["1 spp", "16 spp"],
        row1_path
    )
    
    print(f"Saved comparison row1 → {row1_path}")
    
    row2_path = os.path.join(output_dir, "comparison_row2.png")

    save_row_comparison(
        [img_64, img_gt],
        ["64 spp", "4096 spp"],
        row2_path
    )
    
    print(f"Saved comparison row2 → {row2_path}")


# ----------------------------
# Entry Point
# ----------------------------

if __name__ == "__main__":
    main()