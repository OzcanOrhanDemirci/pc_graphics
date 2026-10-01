"""Noise map from two independent renders of the same frame: |a - b| / (a + b) per pixel, shown as an image.

  python tools/noisemap.py a.exr b.exr out.png
Also prints the mean relative noise in a grid of regions so the noisiest parts can be named.
"""
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from exrstats import read_exr  # noqa: E402


def lum(img):
    return 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]


a, b = lum(read_exr(sys.argv[1])), lum(read_exr(sys.argv[2]))
m = 0.5 * (a + b)
rel = np.abs(a - b) / (m + 0.02 * m.mean() + 1e-12)
img = np.clip(rel / 1.0, 0, 1)
Image.fromarray((np.sqrt(img) * 255).astype(np.uint8)).save(sys.argv[3])
H, W = rel.shape
print(f"mean relative noise {rel.mean():.4f}")
for gy in range(3):
    row = []
    for gx in range(4):
        r = rel[gy * H // 3:(gy + 1) * H // 3, gx * W // 4:(gx + 1) * W // 4]
        row.append(f"{r.mean():.3f}")
    print("  " + "  ".join(row))
