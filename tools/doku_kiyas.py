"""Koy's texture modes side by side: the same view in Standart (2K), Yuksek (8K) and Ultra (16K), one exposure.

  pc_graphics.exe --scene koy --doku --out captures/koy/doku/x --cam "x y z" --look "x y z" [...]   (x_NN_<mode>.png)
  python tools/doku_kiyas.py captures/koy/doku/x [--uzaklik 0.5] [--ekran-m 0.7195] [--kirp 1024]

For each view and each pair of modes:
  flip   NVIDIA FLIP between the two pictures (a perceptual difference, 0 the same), for the screen given (default
         Ozcan's: 32.5 inch 16:9, 0.7195 m and 2560 px wide) seen from --uzaklik metres, as pixels per degree. Read it
         against Standart>Yuksek of the same view: the 2K -> 8K step Ozcan called "night and day"
  fark   mean |difference| of the 8-bit pixels, and the share of pixels differing by more than 3 levels
  ince   the RMS (grey levels) of the finest screen octave, 0.25-0.5 cycles a pixel, in the centre crop: the detail the
         screen actually shows. Ultra above Yuksek means the 16K put detail on the screen.
Also writes <x>_NN_kirp.png: the centre crops (dusuk | normal | yuksek) at 2x, for a look.
"""
import argparse
import glob
import json
import os
import re

import numpy as np
from PIL import Image

MODES = ('dusuk', 'normal', 'yuksek')


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.float32) / 255.0


def luma(rgb):
    return (0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]) * 255.0


def centre(a, n):
    h, w = a.shape[:2]
    n = min(n, h, w)
    return a[(h - n) // 2:(h - n) // 2 + n, (w - n) // 2:(w - n) // 2 + n]


def octave_rms(img, n):
    """grey-level RMS of the finest and the next screen octave in the centre crop (Hann window compensated)"""
    c = centre(luma(img), n).astype(np.float64)
    n = c.shape[0]
    c -= c.mean()
    win = np.hanning(n)
    c *= win[:, None] * win[None, :]
    p = np.abs(np.fft.fft2(c)) ** 2 / float(n) ** 4 / (np.mean(win ** 2) ** 2)
    f = np.fft.fftfreq(n)
    r = np.sqrt(f[:, None] ** 2 + f[None, :] ** 2)
    return float(np.sqrt(p[(r >= 0.25) & (r < 0.5)].sum())), float(np.sqrt(p[(r >= 0.125) & (r < 0.25)].sum()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('onek', help='the --out of pc_graphics --doku (without _NN_<mode>.png)')
    ap.add_argument('--uzaklik', type=float, default=0.5, help='eye to screen, metres')
    ap.add_argument('--ekran-m', type=float, default=0.7195, help='the screen width, metres (32.5 inch 16:9)')
    ap.add_argument('--kirp', type=int, default=1024, help='the centre crop for "ince", pixels')
    ap.add_argument('--kipler', default=','.join(MODES), help='the picture names, in order (pc_graphics --dalgacik: kapali,acik)')
    a = ap.parse_args()
    import flip_evaluator as flip

    modes = tuple(a.kipler.split(','))
    pairs = [(modes[i], modes[i + 1]) for i in range(len(modes) - 1)] + ([(modes[0], modes[-1])] if len(modes) > 2 else [])
    views = sorted({int(m.group(1)) for p in glob.glob(a.onek + '_*_*.png')
                    if (m := re.search(r'_(\d\d)_(' + '|'.join(map(re.escape, modes)) + r')\.png$', p))})
    out = {'uzaklik_m': a.uzaklik, 'ekran_m': a.ekran_m, 'gorunumler': {}}
    for v in views:
        imgs = {m: load(f'{a.onek}_{v:02d}_{m}.png') for m in modes if os.path.exists(f'{a.onek}_{v:02d}_{m}.png')}
        if not imgs:
            continue
        width = next(iter(imgs.values())).shape[1]
        ppd = a.uzaklik * np.tan(np.radians(1.0)) * width / a.ekran_m
        res = {'ppd': round(float(ppd), 1), 'ince': {}, 'ciftler': {}}
        for m, im in imgs.items():
            fine, next_ = octave_rms(im, a.kirp)
            res['ince'][m] = {'en_ince_oktav': round(fine, 3), 'bir_alt_oktav': round(next_, 3)}
        for ra, rb in pairs:
            if ra not in imgs or rb not in imgs:
                continue
            # the viewing condition as pixels per degree (flip_evaluator's "vc" is [distance m, width PIXELS, width m]: an
            # easy order to get wrong, and FLIP then silently runs at ~0 ppd); checked against what FLIP says it used
            emap, mean, used = flip.evaluate(imgs[ra], imgs[rb], 'LDR', inputsRGB=True, applyMagma=False, computeMeanError=True,
                                             parameters={'ppd': float(ppd)})
            if abs(float(used.get('ppd', -1.0)) - ppd) > 0.01:
                raise SystemExit(f"FLIP baska bir ppd kullandi: {used.get('ppd')} (istenen {ppd:.2f})")
            emap = np.asarray(emap).reshape(imgs[ra].shape[:2])
            d = np.abs(np.round(imgs[ra] * 255.0) - np.round(imgs[rb] * 255.0)).max(axis=2)
            res['ciftler'][f'{ra}>{rb}'] = {
                'flip': round(float(mean), 4),
                'flip_orta': round(float(centre(emap, a.kirp).mean()), 4),
                'flip_p99': round(float(np.percentile(emap, 99)), 4),
                'fark_ort': round(float(d.mean()), 3),
                'fark_3ten_buyuk': round(float((d > 3).mean()), 4),
            }
        out['gorunumler'][f'{v:02d}'] = res
        # the centre crops at 2x, dusuk | normal | yuksek
        tiles = [np.kron(centre((imgs[m] * 255.0).astype(np.uint8), 512), np.ones((2, 2, 1), dtype=np.uint8)) for m in modes if m in imgs]
        Image.fromarray(np.concatenate(tiles, axis=1)).save(f'{a.onek}_{v:02d}_kirp.png')
        line = '  '.join(f"{k}: flip {c['flip']:.4f} (orta {c['flip_orta']:.4f}), fark {c['fark_ort']:.2f}, >3 %{100 * c['fark_3ten_buyuk']:.1f}"
                         for k, c in res['ciftler'].items())
        fines = '  '.join(f"{m} {r['en_ince_oktav']:.2f}" for m, r in res['ince'].items())
        print(f'gorunum {v:02d} ({ppd:.1f} ppd): ince oktav [{fines}]')
        print(f'   {line}')
    with open(a.onek + '_olcum.json', 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print('yazildi:', a.onek + '_olcum.json')


if __name__ == '__main__':
    main()
