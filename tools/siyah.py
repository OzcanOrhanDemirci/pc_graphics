"""Karanlığın temizliği (Ateş İ1): bir ateşin kareleri, aynı anın referansına karşı.

  python tools/siyah.py <klasor> [<klasor> ...]

Klasörü sahnenin kendisi yazar: `--seri N --dondur` (kare_*.png; ısınma kareleri ateş yanarken, sonra ateş durur, yalnız
örnekler kıpırdar; ref.png: o an DLSS'siz, 16 alt örnek, ışığı her birinde 64 örnekle). İlk kare canlının hâlidir (DLSS
geçmişi yanan ateşte birikti), son kare durgunda yakınsamışı. Ölçüler 8 bitlik kod değerinde (0-255), kanalların en
parlağında (ateşin ışığı kızıl: yeşil ve mavi karanlıkta hep sıfıra yakın), piksellerin referanstaki parlaklığına göre
bantlarda:
  gren      kare ile referans farkının 3x3 ortalamadan sapması: tek kare içindeki tanecik ve fırça izi
  sapma     karenin referanstan farkının ortalaması: DLSS'in yanlılığı (canlıda kararma)
  titreme   zamanda ikinci fark |L_t - (L_t-1 + L_t+1) / 2|: yakınsamanın düzgün eğilimi düşer, pır pır kalır
  gökte     referansta 1 seviyenin altındaki pikseller: "temiz siyah" (ölçüt: <= 1 seviye)
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float32).max(axis=2)


def box3(a):
    p = np.pad(a, 1, mode="edge")
    s = sum(p[1 + dy:p.shape[0] - 1 + dy, 1 + dx:p.shape[1] - 1 + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
    return s / 9.0


for d in sys.argv[1:]:
    fs = sorted(glob.glob(os.path.join(d, "kare_*.png")))
    R = load(os.path.join(d, "ref.png"))
    A = np.stack([load(f) for f in fs])
    e = np.abs(A[1:-1] - 0.5 * (A[:-2] + A[2:])).mean(axis=0)
    g0, gN = (np.abs((a - R) - box3(a - R)) for a in (A[0], A[-1]))
    b0, bN = A[0] - R, A[-1] - R
    print(f"{d} ({len(fs)} kare)               ilk kare (canli)         son kare (durgun)")
    for lo, hi in [(1, 4), (4, 12), (12, 32), (32, 80), (80, 256)]:
        k = (R >= lo) & (R < hi)
        if not k.any():
            continue
        print(f"  ref {lo:3d}-{hi:3d}: {k.mean() * 100:5.1f}%   gren {g0[k].mean():5.2f} sapma {b0[k].mean() / max(R[k].mean(), 1e-6) * 100:+4.0f}%   "
              f"gren {gN[k].mean():5.2f} sapma {bN[k].mean() / max(R[k].mean(), 1e-6) * 100:+4.0f}%   titreme {e[k].mean():5.2f}")
    k = R < 1
    v = A[:, k]
    print(f"  gokte: en buyuk {v.max():.0f}, >1 seviye %{(v > 1).mean() * 100:.3f}, ortalama {v.mean():.2f}")
