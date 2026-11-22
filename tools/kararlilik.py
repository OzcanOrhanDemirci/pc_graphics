"""Kararlılık ve netlik ölçüsü (Koy PLAN §9): bir kamera yolunun CANLI kareleri, aynı anların YAKINSAMIŞ referansına karşı.

  python tools/kararlilik.py <klasor> [--atla N] [--flip-her K] [--isi]

Klasörde live_0000.png, live_0001.png ... ve ref_0000.png ... (aynı sayıda, aynı anlar). Kareleri sahnenin kendi aracı yazar
(Fener: --kararlilik, Koy: --kararlilik); canlı kare pencerenin çizdiği yoldan, referans aynı kamera ve aynı dünya anında çok örnekle.

Ölçüler, görüntü parlaklığı 0..1 (8 bit sRGB kodlu değerlerin Rec.709 ağırlıklı toplamı / 255: göze yakın açıklık):
  kıpırtı (piksel)    e_t = (L_t - L_t-1) - (R_t - R_t-1): canlının referanstan FAZLA kıpırdaması. Kamera ya da dünya kıpırdarken
                      gerçek değişim referansta da vardır ve düşülür; geriye örneklemenin, gürültünün, zamansal hilenin kıpırtısı kalır.
                      |e| ortalaması, %95, %99 (bütün pikseller, bütün kare çiftleri).
  kıpırtı (8x8 blok)  aynısı 8x8 blok ortalamalarıyla: ince gren blokta söner, kayan leke ve dalgalanma sönmez (Laboratuvar revize 1).
  netlik              FLIP(R_t, L_t) (NVIDIA, LDR): bulanıklaşarak "kararlı" olmak burada yakalanır. Ortalama ve en kötü kare.
  bağıl kıpırtı       piksel kıpırtısı / referansın kendi değişimi |R_t - R_t-1| ortalaması (yol ne kadar hareketliyse o kadar hoşgörü).
  yalnız canlı        |L_t - L_t-1| ortalaması (referanssız, eski ölçüyle karşılaştırmak için).
--isi: kıpırtının zamana göre ortalaması (piksel başına) ısı haritası, klasöre kipirti_isi.png.
"""
import argparse
import glob
import json
import os

import numpy as np
from PIL import Image


def luma(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2], a


def blocks(l, b=8):
    h, w = (l.shape[0] // b) * b, (l.shape[1] // b) * b
    return l[:h, :w].reshape(h // b, b, w // b, b).mean(axis=(1, 3))


def pct(a, q):
    return float(np.percentile(a, q))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--atla", type=int, default=0, help="baştan atılacak kare (zamansal geçmişin dolması)")
    ap.add_argument("--flip-her", type=int, default=4, help="FLIP her K karede bir (0: hiç)")
    ap.add_argument("--isi", action="store_true")
    a = ap.parse_args()
    lives = sorted(glob.glob(os.path.join(a.dir, "live_*.png")))
    refs = sorted(glob.glob(os.path.join(a.dir, "ref_*.png")))
    n = min(len(lives), len(refs))
    if n < a.atla + 3:
        raise SystemExit(f"yetersiz kare: {len(lives)} canli, {len(refs)} referans")
    lives, refs = lives[a.atla:n], refs[a.atla:n]
    n = len(lives)
    flip = None
    if a.flip_her > 0:
        import flip_evaluator as flip
    pix_abs, blk_abs, ref_chg, live_chg = [], [], [], []
    heat = None
    flips = []
    pl = pr = bl = br = None
    for k in range(n):
        L, Lrgb = luma(lives[k])
        R, Rrgb = luma(refs[k])
        if L.shape != R.shape:
            raise SystemExit(f"boyut farkli: {lives[k]} {refs[k]}")
        BL, BR = blocks(L), blocks(R)
        if pl is not None:
            e = (L - pl) - (R - pr)
            ae = np.abs(e)
            pix_abs.append(ae.astype(np.float32).ravel()[::7])   # seyreltilmiş örnek: yüzdelikler için yeter
            blk_abs.append(np.abs((BL - bl) - (BR - br)).ravel())
            ref_chg.append(float(np.abs(R - pr).mean()))
            live_chg.append(float(np.abs(L - pl).mean()))
            if a.isi:
                heat = ae if heat is None else heat + ae
            pix_mean_k = float(ae.mean())
            pix_abs[-1] = np.concatenate([pix_abs[-1], [pix_mean_k]])  # son öge: karenin ortalaması (aşağıda ayrılır)
        if flip is not None and k % a.flip_her == 0:
            _, m, _ = flip.evaluate(Rrgb, Lrgb, "LDR")
            flips.append((k, float(m)))
        pl, pr, bl, br = L, R, BL, BR
    frame_means = np.array([p[-1] for p in pix_abs])
    samples = np.concatenate([p[:-1] for p in pix_abs])
    blk = np.concatenate(blk_abs)
    res = {
        "kare": n,
        "kipirti_piksel": {"ort": float(frame_means.mean()), "p95": pct(samples, 95), "p99": pct(samples, 99),
                           "en_kotu_kare_ort": float(frame_means.max())},
        "kipirti_blok": {"ort": float(blk.mean()), "p95": pct(blk, 95), "p99": pct(blk, 99)},
        "referans_degisim_ort": float(np.mean(ref_chg)),
        "bagil_kipirti": float(frame_means.mean() / max(np.mean(ref_chg), 1e-6)),
        "yalniz_canli_degisim_ort": float(np.mean(live_chg)),
    }
    if flips:
        fv = np.array([f for _, f in flips])
        worst = max(flips, key=lambda x: x[1])
        res["netlik_flip"] = {"ort": float(fv.mean()), "en_kotu": worst[1], "en_kotu_kare": worst[0] + a.atla, "olculen_kare": len(flips)}
    print(json.dumps(res, indent=2, ensure_ascii=False))
    with open(os.path.join(a.dir, "kararlilik.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, indent=2, ensure_ascii=False)
    if a.isi and heat is not None:
        h = heat / max(1, n - 1)
        v = np.clip(h * 20.0, 0, 1)   # 0.05 ortalama kıpırtı = tam kırmızı/sarı
        img = np.stack([np.clip(2 * v, 0, 1), np.clip(2 * v - 1, 0, 1), 0.3 * (1 - v)], axis=-1)
        Image.fromarray((img * 255).astype(np.uint8)).save(os.path.join(a.dir, "kipirti_isi.png"))


if __name__ == "__main__":
    main()
