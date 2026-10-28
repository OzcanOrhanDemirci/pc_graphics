"""Titreme ölçüsü (Kuyu Ö3'ün eki): süzgeçten bağımsız, zamanda ikinci fark.

  python tools/titreme.py <klasor> [<klasor> ...]

kararlilik.py'nin kıpırtısı canlının değişimini referansınkiyle kıyaslar: canlı referanstan farklı bir süzgeçle
(daha keskin ya da daha yumuşak) çizilmişse, hiç titremese de hareketli ince ayrıntıda "fazla değişim" sayılır.
Bu araç her dizinin kendi zamansal ikinci farkını ölçer: e_t = |L_t - (L_(t-1) + L_(t+1)) / 2|. Sabit hızla kayan
bir görüntüde (süzgeç ne olursa olsun, sürekliyse) küçüktür; bir nokta örneklere bir çarpıp bir kaçınca (titreme),
bir kenar piksel merkezinden atlayınca büyür. Klasörde live_*.png ve ref_*.png: ikisi için ayrı ayrı, oran = canlı /
referans (1'e yakınsa canlı, çok örnekli referans kadar sakin).
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def luma(p):
    a = np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def second(files):
    ls = [luma(f) for f in files]
    e = [np.abs(ls[t] - 0.5 * (ls[t - 1] + ls[t + 1])) for t in range(1, len(ls) - 1)]
    e = np.stack(e)
    return float(e.mean()), float(np.percentile(e, 99))


for d in sys.argv[1:]:
    # (Ateş'in `--seri N --dondur --donus D` klibinde canlı kare_*.png)
    lv = sorted(glob.glob(os.path.join(d, "live_*.png"))) or sorted(glob.glob(os.path.join(d, "kare_*.png")))
    rf = sorted(glob.glob(os.path.join(d, "ref_*.png")))
    a, a99 = second(lv)
    b, b99 = second(rf)
    # (hiçbir şeyin kıpırdamadığı bir yolda referans hiç değişmez: oran yok)
    oran = f"{a / b:.2f}" if b > 0 else "yok (referans durgun)"
    print(f"{d}: canli {a:.5f} (p99 {a99:.4f})  referans {b:.5f} (p99 {b99:.4f})  oran {oran}")
