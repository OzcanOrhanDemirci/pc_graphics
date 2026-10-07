"""Kuyu Ö2: tek yönlü duvarın tabloları, bağımsız bir hesapla (PLAN §4).

  python tools/kuyu_o2.py captures/kuyu/o2/o2_tablolar.csv

Uygulama (kuyu_app.cpp) gümüş filmi iki arayüzün Airy toplamıyla hesaplar. Bu araç aynı fiziği başka yoldan kurar:
Abelès karakteristik matrisi (eğik admitanslar, s ve p ayrı), aynı ölçülmüş sabitlerle; duvarın katmanlarını
(yansıma önleyici, gri cam, film) aynı sırayla birleştirir ve tablolarla karşılaştırır. Ayrıca:
  - filmde enerji: R + T + A = 1 ve A >= 0,
  - karşılıklılık: filmin geçirgenliği havadan (θ) ve camdan (θ_t) aynı,
  - içeriden / dışarıdan yansıtma oranı (tek yönlülüğün kendisi).
Hedef: her tablo değeri ±0,005.
"""
import csv
import sys

import numpy as np

LAM = np.array([0.610, 0.550, 0.465])
AG = np.array([0.058 + 4.094j, 0.060 + 3.597j, 0.0469 + 2.803j])
CU = np.array([0.378 + 3.107j, 1.007 + 2.582j, 1.247 + 2.456j])
CR = np.array([3.1935 + 4.266j, 2.7892 + 4.1977j, 2.1410 + 3.9299j])
AU = np.array([0.226 + 3.192j, 0.424 + 2.472j, 1.332 + 1.869j])
FILM = 0.045        # um (scenes/kuyu.cfg wall.film)
GLASS = 12700.0     # um
TV, RS = 0.18, 0.0426
RAR = 0.005


def glass_n(l):
    return 1.5130 - 0.003169 * l * l + 0.003962 / (l * l)


def film_abeles(n0, n1, n2, d, lam, c0):
    """Abelès: one layer between n0 and n2; returns R, T (unpolarised) for cos(theta0) = c0 in n0 (real). The matrix is
    Macleod's, in his sign convention N = n - ik: the measured n + ik enters conjugated."""
    n1 = np.conj(n1)
    s0 = np.sqrt(max(0.0, 1.0 - c0 * c0))
    c1 = np.sqrt(1 - (n0 * s0 / n1) ** 2 + 0j)
    c2 = np.sqrt(1 - (n0 * s0 / n2) ** 2 + 0j)
    d1 = 2 * np.pi * n1 * c1 * d / lam
    out = []
    for pol in ("s", "p"):
        if pol == "s":
            e0, e1, e2 = n0 * c0, n1 * c1, n2 * c2
        else:
            e0, e1, e2 = n0 / c0, n1 / c1, n2 / c2
        M = np.array([[np.cos(d1), 1j * np.sin(d1) / e1], [1j * e1 * np.sin(d1), np.cos(d1)]])
        B, C = M @ np.array([1.0, e2])
        r = (e0 * B - C) / (e0 * B + C)
        t = 2 * e0 / (e0 * B + C)
        out.append((abs(r) ** 2, (e2.real / e0.real) * abs(t) ** 2))
    return 0.5 * (out[0][0] + out[1][0]), 0.5 * (out[0][1] + out[1][1])


def fresnel(n1, n2, c):
    s2 = 1 - c * c
    ct = np.sqrt(1 - n1 * n1 * s2 / (n2 * n2) + 0j)
    rs = (n1 * c - n2 * ct) / (n1 * c + n2 * ct)
    rp = (n2 * c - n1 * ct) / (n2 * c + n1 * ct)
    return 0.5 * (abs(rs) ** 2 + abs(rp) ** 2)


def main():
    rows = list(csv.DictReader(open(sys.argv[1], encoding="utf-8")))
    tau127 = TV / ((1 - RS) ** 2)
    alpha = -np.log(tau127) / 12700.0
    worst = {k: 0.0 for k in ("inR", "outR", "T", "cu", "cr", "au", "varnish")}
    energy_worst, recip_worst = 0.0, 0.0
    ratio = []
    for row in rows:
        c = float(row["cos"])
        if c < 0.02:   # (at the very graze the tables' last interval; reported apart)
            continue
        s = np.sqrt(1 - c * c)
        for ch in range(3):
            ng = glass_n(LAM[ch])
            cg = np.sqrt(1 - (s / ng) ** 2)
            Rin, Tin = film_abeles(1.0, AG[ch], ng, FILM, LAM[ch], c)
            Rg, Tg = film_abeles(ng, AG[ch], 1.0, FILM, LAM[ch], cg)
            energy_worst = max(energy_worst, -(1 - Rin - Tin))   # an absorption below zero
            recip_worst = max(recip_worst, abs(Tin - Tg))
            tau = np.exp(-alpha * GLASS / cg)
            far = fresnel(1.0, ng, c) * (RAR / fresnel(1.0, ng, 1.0))
            T = (1 - far) * tau * Tin
            Rout = far + (1 - far) ** 2 * tau * tau * Rg
            vals = {"inR": Rin, "outR": Rout, "T": T, "cu": fresnel(1.0, CU[ch], c), "cr": fresnel(1.0, CR[ch], c), "au": fresnel(1.0, AU[ch], c)}
            for k, v in vals.items():
                worst[k] = max(worst[k], abs(float(row[f"{k}{ch}"]) - v))
            if ch == 1 and abs(c - 1.0) < 1e-6:
                ratio = [Rin, Rout, T, Rin + Tin]
        worst["varnish"] = max(worst["varnish"], abs(float(row["varnish"]) - fresnel(1.0, 1.5, c)))
    print("O2 bagimsiz hesap (Abeles matrisi) <-> uygulamanin tablolari, en buyuk fark:")
    for k, v in worst.items():
        print(f"   {k:8s} {v:.6f}")
    print(f"   film: soğurma A = 1 - R - T en kucuk {-energy_worst:.4f} (>= 0 olmali); karsiliklilik |T_hava - T_cam| en cok {recip_worst:.2e}")
    if ratio:
        print(f"   dik gelis (550 nm): iceriden yansitma {ratio[0]:.4f}, disaridan {ratio[1]:.4f} (oran {ratio[0] / ratio[1]:.1f}), "
              f"gecirgenlik {ratio[2]:.5f}")
    ok = max(worst.values()) <= 0.005 and recip_worst < 1e-6 and energy_worst <= 0.0
    print("O2:", "TUTTU" if ok else "TUTMADI", "(hedef +-0,005)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
