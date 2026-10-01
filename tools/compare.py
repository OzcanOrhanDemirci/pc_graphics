"""Error of a render against a reference (both linear sRGB EXR from stress.exe).

  python tools/compare.py test.exr reference.exr [--exposure 2^EV] [--flip]
Prints RMSE and relative RMSE of luminance, mean luminance difference (bias), and optionally NVIDIA FLIP on the
AgX-free sRGB images (FLIP's own HDR mode on the linear images).
"""
import argparse
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from exrstats import read_exr  # noqa: E402


def lum(img):
    return 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("test")
    ap.add_argument("ref")
    ap.add_argument("--flip", action="store_true")
    ap.add_argument("--crop", type=int, nargs=4)
    a = ap.parse_args()
    t = read_exr(a.test)[..., :3]
    r = read_exr(a.ref)[..., :3]
    if a.crop:
        x0, y0, x1, y1 = a.crop
        t, r = t[y0:y1, x0:x1], r[y0:y1, x0:x1]
    lt, lr = lum(t), lum(r)
    mse = np.mean((lt - lr) ** 2)
    rel = np.sqrt(mse) / max(lr.mean(), 1e-12)
    # relative error per pixel, robust to the bright patches dominating
    relpix = np.abs(lt - lr) / (lr + 0.01 * lr.mean())
    print(f"RMSE {np.sqrt(mse):.4g}  relRMSE {rel:.4f}  mean rel. pixel error {relpix.mean():.4f}  "
          f"bias {(lt.mean() - lr.mean()) / lr.mean():+.4%}")
    if a.flip:
        import flip_evaluator as flip
        scale = 0.18 / max(np.median(lr), 1e-9)
        err, mean_err, _ = flip.evaluate((r * scale).astype(np.float32), (t * scale).astype(np.float32), "HDR")
        print(f"HDR-FLIP mean {mean_err:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
