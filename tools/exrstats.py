"""Statistics of a linear sRGB EXR written by pc_graphics.exe: luminance mean / percentiles, optional region.

  python tools/exrstats.py captures/x.exr [--crop x0 y0 x1 y1] [--expect 1.0]
Exit code 1 when --expect is given and the mean luminance misses it by more than --tol (relative).
"""
import argparse
import sys

import numpy as np

def read_exr(path):
    import OpenEXR
    with OpenEXR.File(path) as f:
        ch = f.channels()
        if "RGBA" in ch:
            return np.asarray(ch["RGBA"].pixels, dtype=np.float64)
        if "RGB" in ch:
            return np.asarray(ch["RGB"].pixels, dtype=np.float64)
        r, g, b = (np.asarray(ch[c].pixels, dtype=np.float64) for c in "RGB")
        return np.stack([r, g, b], axis=-1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--crop", type=int, nargs=4)
    ap.add_argument("--expect", type=float)
    ap.add_argument("--tol", type=float, default=0.01)
    a = ap.parse_args()
    img = read_exr(a.path)
    if a.crop:
        x0, y0, x1, y1 = a.crop
        img = img[y0:y1, x0:x1]
    rgb = img[..., :3]
    Y = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    finite = np.isfinite(Y)
    Yf = Y[finite]
    p = np.percentile(Yf, [1, 10, 50, 90, 99, 99.9])
    print(f"{a.path}: {Y.shape[1]}x{Y.shape[0]}  nonfinite {np.size(Y) - Yf.size}")
    print(f"  Y mean {Yf.mean():.5f}  std {Yf.std():.5f}  min {Yf.min():.4g}  max {Yf.max():.4g}")
    print("  percentiles 1/10/50/90/99/99.9: " + " ".join(f"{v:.4g}" for v in p))
    print(f"  mean RGB {rgb[..., 0][finite].mean():.4f} {rgb[..., 1][finite].mean():.4f} {rgb[..., 2][finite].mean():.4f}")
    if a.expect is not None:
        err = abs(Yf.mean() - a.expect) / a.expect
        ok = err <= a.tol
        print(f"  expect {a.expect}: relative error {err:.4%} -> {'OK' if ok else 'FAIL'}")
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
