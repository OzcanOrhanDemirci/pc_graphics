"""Kararlılık ve netlik ölçüsü (Koy PLAN §9): bir kamera yolunun CANLI kareleri, aynı anların YAKINSAMIŞ referansına karşı.

  python tools/kararlilik.py <klasor> [--atla N] [--flip-her K] [--isi] [--hareket-telafili]

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
--hareket-telafili (Rüya): klasörde depth_0000.bin ... (sahnenin --dump-depth çıktısı: kare başına kamera ve derinlik) varsa,
  önceki kare derinlik + kamera pozuyla yeniden izdüşürülür (çift doğrusal), örtülen/açılan pikseller (derinlik uyuşmazlığı,
  ekran dışı, gökyüzü/yüzey karışımı) maskelenir; geriye kalan pikselde kıpırtı
  e_mc = (L_t - W[L_t-1]) - (R_t - W[R_t-1]) (W: aynı izdüşüm). Büyük kaydırmada canlı/referans durağan farkı (bulanıklık) iki
  tarafta da aynı örnekleme paylaştığından düşer. Çıktıya "kipirti_hareket_telafili" (+ "maske_orani", "referans_mc_degisim_ort")
  eklenir (referans_ham_degisim_maskeli_ort: aynı maskede telafisiz |R_t - R_t-1|, izdüşümün işe yaradığını gösterir);
  mevcut anahtarlar aynen kalır.
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


def load_depth(path):
    """depth_XXXX.bin (ruya_app.cpp writeDepth): width, height, eye, W, U, V, then view-space depth per pixel (-1 sky)."""
    with open(path, "rb") as f:
        w, h = np.frombuffer(f.read(8), dtype=np.uint32)
        pose = np.frombuffer(f.read(48), dtype=np.float32).astype(np.float64)
        z = np.frombuffer(f.read(int(w) * int(h) * 4), dtype=np.float32).reshape(int(h), int(w))
    return {"eye": pose[0:3], "W": pose[3:6], "U": pose[6:9], "V": pose[9:12], "z": z}


def warp_to_previous(cur, prev, depth_tol):
    """Where each pixel of the current frame sits in the previous frame, and which of them are comparable.

    Returns (x0, y0, wx, wy, valid): top-left neighbour index, bilinear weights, and the mask. A pixel is valid when its
    four bilinear neighbours in the previous frame lie on the same surface (view depth within depth_tol, relative) or
    are all sky: that drops occluded and disoccluded pixels, silhouettes and everything that left the frame."""
    h, w = cur["z"].shape
    xs = 2.0 * (np.arange(w) + 0.5) / w - 1.0
    ys = 1.0 - 2.0 * (np.arange(h) + 0.5) / h
    ray = (cur["W"][None, None, :] + xs[None, :, None] * cur["U"][None, None, :] + ys[:, None, None] * cur["V"][None, None, :])
    z = cur["z"].astype(np.float64)
    sky = z < 0
    # Geometry: offset from the previous eye. Sky is at infinity, so only its direction matters.
    rel = np.where(sky[..., None], ray, ray * z[..., None] + (cur["eye"] - prev["eye"])[None, None, :])
    zp = rel @ prev["W"]
    ok = zp > 1e-3
    zs = np.where(ok, zp, 1.0)
    xp = (rel @ prev["U"]) / (prev["U"] @ prev["U"]) / zs
    yp = (rel @ prev["V"]) / (prev["V"] @ prev["V"]) / zs
    fx = (xp + 1.0) * 0.5 * w - 0.5
    fy = (1.0 - yp) * 0.5 * h - 0.5
    ok &= (fx >= 0) & (fx <= w - 1) & (fy >= 0) & (fy <= h - 1)
    x0 = np.clip(np.floor(fx).astype(np.int64), 0, w - 2)
    y0 = np.clip(np.floor(fy).astype(np.int64), 0, h - 2)
    wx = (fx - x0).astype(np.float32)
    wy = (fy - y0).astype(np.float32)
    pz = prev["z"]
    for dy in (0, 1):
        for dx in (0, 1):
            n = pz[y0 + dy, x0 + dx].astype(np.float64)
            n_sky = n < 0
            same = np.where(sky, n_sky, ~n_sky & (np.abs(n - zp) <= depth_tol * zp))
            ok &= same
    return x0, y0, wx, wy, ok


def sample_bilinear(img, x0, y0, wx, wy):
    a = img[y0, x0] * (1 - wx) + img[y0, x0 + 1] * wx
    b = img[y0 + 1, x0] * (1 - wx) + img[y0 + 1, x0 + 1] * wx
    return a * (1 - wy) + b * wy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--atla", type=int, default=0, help="baştan atılacak kare (zamansal geçmişin dolması)")
    ap.add_argument("--flip-her", type=int, default=4, help="FLIP her K karede bir (0: hiç)")
    ap.add_argument("--isi", action="store_true")
    ap.add_argument("--hareket-telafili", action="store_true", help="depth_XXXX.bin ile hareket telafili kipirti (Ruya)")
    ap.add_argument("--derinlik-tol", type=float, default=0.015, help="yeniden izdusumde goreli derinlik uyusmazligi esigi")
    a = ap.parse_args()
    lives = sorted(glob.glob(os.path.join(a.dir, "live_*.png")))
    refs = sorted(glob.glob(os.path.join(a.dir, "ref_*.png")))
    n = min(len(lives), len(refs))
    if n < a.atla + 3:
        raise SystemExit(f"yetersiz kare: {len(lives)} canli, {len(refs)} referans")
    lives, refs = lives[a.atla:n], refs[a.atla:n]
    n = len(lives)
    depths = None
    if a.hareket_telafili:
        depth_files = sorted(glob.glob(os.path.join(a.dir, "depth_*.bin")))[a.atla:a.atla + n]
        if len(depth_files) != n:
            raise SystemExit(f"--hareket-telafili: {n} kare icin {len(depth_files)} derinlik dosyasi (sahneyi --dump-depth ile kosun)")
        depths = depth_files
    mc_abs, mc_frame, mc_ref_chg, mc_ref_raw, mc_kept = [], [], [], [], []
    prev_depth = None
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
        if depths is not None:
            cur_depth = load_depth(depths[k])
            if cur_depth["z"].shape != L.shape:
                raise SystemExit(f"derinlik boyutu goruntuyle ayni degil: {depths[k]}")
            if prev_depth is not None:
                x0, y0, wx, wy, ok = warp_to_previous(cur_depth, prev_depth, a.derinlik_tol)
                e_mc = ((L - sample_bilinear(pl, x0, y0, wx, wy)) - (R - sample_bilinear(pr, x0, y0, wx, wy)))[ok]
                mc_kept.append(float(ok.mean()))
                if e_mc.size:
                    ae_mc = np.abs(e_mc).astype(np.float32)
                    mc_abs.append(ae_mc[::7])
                    mc_frame.append(float(ae_mc.mean()))
                    mc_ref_chg.append(float(np.abs(R[ok] - sample_bilinear(pr, x0, y0, wx, wy)[ok]).mean()))
                    mc_ref_raw.append(float(np.abs(R[ok] - pr[ok]).mean()))
            prev_depth = cur_depth
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
    if depths is not None:
        if not mc_frame:
            raise SystemExit("--hareket-telafili: hicbir karede karsilastirilabilir piksel kalmadi")
        mc_samples = np.concatenate(mc_abs)
        res["kipirti_hareket_telafili"] = {"ort": float(np.mean(mc_frame)), "p95": pct(mc_samples, 95), "p99": pct(mc_samples, 99),
                                           "en_kotu_kare_ort": float(np.max(mc_frame)), "maske_orani": float(1.0 - np.mean(mc_kept)),
                                           "referans_mc_degisim_ort": float(np.mean(mc_ref_chg)),
                                           "referans_ham_degisim_maskeli_ort": float(np.mean(mc_ref_raw)),
                                           "bagil": float(np.mean(mc_frame) / max(np.mean(mc_ref_chg), 1e-6))}
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
