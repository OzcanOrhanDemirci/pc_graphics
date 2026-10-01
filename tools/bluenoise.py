"""Void-and-cluster blue noise (Ulichney 1993), 128 x 128, toroidal. Writes assets/bluenoise128.png.

The dither in the display transform reads it; it is generated once and committed.
Check: the power spectrum's low-frequency energy is printed relative to white noise (should be << 1).
"""
import pathlib
import sys

import numpy as np
from PIL import Image

N = 128
SIGMA = 1.9


def kernel():
    y, x = np.mgrid[0:N, 0:N]
    dx = np.minimum(x, N - x)
    dy = np.minimum(y, N - y)
    return np.exp(-(dx * dx + dy * dy) / (2 * SIGMA * SIGMA))


def main():
    rng = np.random.default_rng(12345)
    K = kernel()
    Kf = np.fft.rfft2(K)

    def energy(pattern):
        return np.fft.irfft2(np.fft.rfft2(pattern) * Kf, s=(N, N))

    def shifted(y, x):
        return np.roll(np.roll(K, y, axis=0), x, axis=1)

    # initial pattern: 10% ones, relaxed until stable
    pat = (rng.random((N, N)) < 0.1).astype(np.float64)
    e = energy(pat)
    for _ in range(N * N):
        cl = np.argmax(np.where(pat == 1, e, -np.inf))
        y, x = divmod(cl, N)
        pat[y, x] = 0
        e -= shifted(y, x)
        vo = np.argmin(np.where(pat == 0, e, np.inf))
        vy, vx = divmod(vo, N)
        if (vy, vx) == (y, x):
            pat[y, x] = 1
            e += shifted(y, x)
            break
        pat[vy, vx] = 1
        e += shifted(vy, vx)
    initial = pat.copy()
    ones = int(initial.sum())
    rank = np.zeros((N, N), dtype=np.int64)

    # phase 1: remove tightest clusters from the initial pattern
    pat = initial.copy()
    e = energy(pat)
    for r in range(ones - 1, -1, -1):
        cl = np.argmax(np.where(pat == 1, e, -np.inf))
        y, x = divmod(cl, N)
        pat[y, x] = 0
        e -= shifted(y, x)
        rank[y, x] = r
    # phase 2 and 3: fill the largest voids up to full
    pat = initial.copy()
    e = energy(pat)
    for r in range(ones, N * N):
        vo = np.argmin(np.where(pat == 0, e, np.inf))
        y, x = divmod(vo, N)
        pat[y, x] = 1
        e += shifted(y, x)
        rank[y, x] = r

    img = ((rank.astype(np.float64) + 0.5) / (N * N) * 256).astype(np.uint8)
    out = pathlib.Path(__file__).resolve().parents[1] / "assets" / "bluenoise128.png"
    out.parent.mkdir(exist_ok=True)
    Image.fromarray(img, mode="L").save(out)

    # spectral check: energy in the lowest frequencies relative to white noise of the same variance
    v = rank.astype(np.float64) / (N * N) - 0.5
    P = np.abs(np.fft.fft2(v)) ** 2
    f = np.fft.fftfreq(N)
    fr = np.sqrt(f[None, :] ** 2 + f[:, None] ** 2)
    low = P[(fr > 0) & (fr < 0.1)].mean()
    white = (v * v).sum()
    hist = np.bincount(img.ravel(), minlength=256)
    print(f"wrote {out}; low-frequency power vs white noise: {low / white:.4f}; histogram min/max per level: {hist.min()}/{hist.max()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
