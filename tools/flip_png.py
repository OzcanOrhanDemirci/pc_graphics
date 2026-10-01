"""LDR-FLIP (NVIDIA, perceptual) between two displayed images (8-bit sRGB PNGs), plus mean absolute error.

  python tools/flip_png.py reference.png test.png [--heatmap out.png]
"""
import argparse

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("test")
    ap.add_argument("--heatmap")
    a = ap.parse_args()
    import flip_evaluator as flip
    r = np.asarray(Image.open(a.ref).convert("RGB"), dtype=np.float32) / 255.0
    t = np.asarray(Image.open(a.test).convert("RGB"), dtype=np.float32) / 255.0
    err, mean, _ = flip.evaluate(r, t, "LDR")
    print(f"LDR-FLIP mean {mean:.4f}  (0 identical, 1 maximal)  MAE {np.abs(r - t).mean() * 255:.2f}/255")
    if a.heatmap:
        e = np.clip(np.asarray(err, dtype=np.float32), 0, 1)
        if e.ndim == 3:
            e = e[..., 0]
        Image.fromarray((e * 255).astype(np.uint8)).save(a.heatmap)


if __name__ == "__main__":
    main()
