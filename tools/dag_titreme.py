"""Sahne 13 Dağ: uzak bandın titremesi. tools/titreme.py bütün kareyi ölçer; yürürken yakın taşların gerçek hareketi orada baskın
olur. Bu araç yalnız seçilen satır bandına bakar (uzak sırtların, göğün olduğu yer: göz 1,3 m/s yürürken burası neredeyse durur),
aynı zamansal ikinci farkı canlı ve referans için ayrı verir.

  python tools/dag_titreme.py <klasor> <ilk_satir> <son_satir> [--isi cikti.png]
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def luma(p, a, b):
    im = np.asarray(Image.open(p).convert("RGB"), dtype=np.float32)[a:b] / 255.0
    return 0.2126 * im[..., 0] + 0.7152 * im[..., 1] + 0.0722 * im[..., 2]


def second(files, a, b):
    ls = [luma(f, a, b) for f in files]
    e = np.stack([np.abs(ls[t] - 0.5 * (ls[t - 1] + ls[t + 1])) for t in range(1, len(ls) - 1)])
    return e


d = sys.argv[1]
a, b = int(sys.argv[2]), int(sys.argv[3])
live = sorted(glob.glob(os.path.join(d, "live_*.png")))
ref = sorted(glob.glob(os.path.join(d, "ref_*.png")))
el = second(live, a, b)
er = second(ref, a, b)
print(f"{d} satir {a}-{b}: canli {el.mean():.5f} (p99 {np.percentile(el, 99):.4f}, p99.9 {np.percentile(el, 99.9):.4f})  "
      f"referans {er.mean():.5f} (p99 {np.percentile(er, 99):.4f}, p99.9 {np.percentile(er, 99.9):.4f})")
if "--isi" in sys.argv:
    h = el.mean(axis=0)
    h = np.clip(h / max(np.percentile(h, 99.9), 1e-4), 0, 1)
    Image.fromarray((h * 255).astype(np.uint8)).save(sys.argv[sys.argv.index("--isi") + 1])
