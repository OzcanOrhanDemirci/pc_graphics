"""Scene 12 "Ateş": the gas test's measures against fire science (docs/ates/PLAN.md §2).

  python tools/ates_olc.py captures/ates/gaz          reads gaz.csv and gaz_eksen.csv (pc_graphics --scene ates --gaz N)

Prints: the mean heat release and radiant fraction, the flame height (the tip's median: 50% intermittency) against
Heskestad's L = 0.235 Q^(2/5) - 1.02 D, the puffing frequency (the strongest peak of the flame's flicker spectrum)
against f = 1.5 / sqrt(D), and the centreline's mean temperature and speed against McCaffrey's three regions.
"""
import csv
import math
import sys

import numpy as np


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else 'captures/ates/gaz'
    D = float(sys.argv[2]) if len(sys.argv) > 2 else 0.4
    warm = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
    rows = [r for r in csv.DictReader(open(base + '.csv')) if float(r['t']) >= warm]
    t = np.array([float(r['t']) for r in rows])
    hrr = np.array([float(r['hrr_kW']) for r in rows])
    rad = np.array([float(r['rad_kW']) for r in rows])
    tip = np.array([float(r['tip_m']) for r in rows])
    slab = np.array([float(r['slab_W']) for r in rows])
    Q = hrr.mean()
    print(f'sure {t[-1] - t[0]:.1f} s (ilk {warm:.1f} s atildi), ornek {len(t)}')
    print(f'isi salimi: ortalama {Q:.1f} kW (sapma {hrr.std():.1f}), isinim payi {rad.mean() / max(Q, 1e-6):.2f} (odun alevi ~0.3)')
    L = np.median(tip)
    Lh = 0.235 * Q ** 0.4 - 1.02 * D
    print(f'alev boyu (ucun medyani, %50 kesiklilik): {L:.3f} m; Heskestad {Lh:.3f} m (D {D:.2f} m, Q {Q:.1f} kW) -> oran {L / max(Lh, 1e-6):.2f}')
    print(f'  ucun dagilimi: %10 {np.percentile(tip, 10):.3f}, %90 {np.percentile(tip, 90):.3f} m')
    dt = np.median(np.diff(t))
    for name, sig in (('alt kusagin isinimi', slab), ('uc', tip), ('isi salimi', hrr)):
        x = sig - sig.mean()
        x = x * np.hanning(len(x))
        spec = np.abs(np.fft.rfft(x))
        fr = np.fft.rfftfreq(len(x), dt)
        band = (fr > 0.3) & (fr < 10.0)
        k = np.argmax(spec[band])
        print(f'kabarma frekansi ({name}): {fr[band][k]:.2f} Hz; beklenen 1.5/sqrt(D) = {1.5 / math.sqrt(D):.2f} Hz')
    try:
        ax = list(csv.DictReader(open(base + '_eksen.csv')))
        z = np.array([float(r['z_m']) for r in ax])
        T = np.array([float(r['T_K']) for r in ax])
        v = np.array([float(r['v_ms']) for r in ax])
        if 'q_Wm' in ax[0]:
            qz = np.array([float(r['q_Wm']) for r in ax])
            cum = np.cumsum(qz) / max(qz.sum(), 1e-9)
            for frac in (0.5, 0.9, 0.95, 0.99):
                print(f'isi salimi yuksekligi: %{int(frac * 100)} {z[int(np.searchsorted(cum, frac))]:.3f} m')
        Q25 = Q ** 0.4
        print('eksen (McCaffrey: z/Q^(2/5) < 0.08 surekli alev, 0.08-0.2 kesikli, > 0.2 tuy; dT surekli ~ 800 K):')
        for zz in (0.05, 0.1, 0.2, 0.3, 0.4, 0.6, 0.8, 1.0, 1.2):
            j = int(np.argmin(np.abs(z - zz)))
            s = z[j] / Q25
            if s < 0.08:
                dT, u = 800.0, 6.84 * s ** 0.5 * Q ** 0.2
            elif s < 0.2:
                dT, u = 63.0 / s, 1.93 * Q ** 0.2
            else:
                dT, u = 21.6 * s ** (-5.0 / 3.0), 1.12 * s ** (-1.0 / 3.0) * Q ** 0.2
            dT = dT * 293.0 / 9.81 / 1.0 if False else dT
            print(f'  z {z[j]:.2f} m (z/Q^0.4 {s:.3f}): T {T[j]:.0f} K (dT {T[j] - 288:.0f}; McCaffrey ~{dT:.0f}), w {v[j]:.2f} m/s (McCaffrey ~{u:.2f})')
    except FileNotFoundError:
        pass


if __name__ == '__main__':
    main()
