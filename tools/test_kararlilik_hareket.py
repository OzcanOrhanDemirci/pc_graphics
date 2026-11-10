"""kararlilik.py --hareket-telafili sınaması: sentetik sahne (zemin + duvar + gökyüzü), çözümlü derinlik, GPU gerekmez.

  python tools/test_kararlilik_hareket.py

Canlı kare pikselin merkez örneği, referans 4x4 örnek ortalaması (aynı sahne, biraz daha yumuşak). Sınanan:
  1. durağan kamera: yeni ölçü ~0
  2. yalnız kamera hareketi, gürültüsüz: yeni ölçü ~0, eski ölçü yüksek (referansla canlının durağan farkı hareketle de kayar)
  3. bilinen titreşim (her karede bağımsız +-A gürültü canlıya): yeni ölçü beklenen değere yakın ve gürültüsüze göre çok yüksek
  4. yeniden izdüşümün kendisi: referansın telafili değişimi, aynı maskede telafisiz değişimden çok küçük olmalı; izdüşüm
     bozulunca (önceki karenin kamerası yerine geçerli kamera) bu fark kaybolur: sınama başarısız olabilir
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kararlilik  # noqa: E402

W, H, FOV = 320, 180, 48.0


def basis(yaw, pitch):
    """Same convention as ruya cameraBasis (ruya_world.cpp)."""
    fwd = np.array([np.sin(yaw) * np.cos(pitch), np.sin(pitch), -np.cos(yaw) * np.cos(pitch)])
    right = np.cross(fwd, [0, 1, 0])
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    scale = np.tan(np.radians(FOV) / 2)
    return fwd, right * (W / H) * scale, up * scale


def texture(x, z, on_wall):
    """Smooth enough that bilinear resampling is accurate, rich enough to move with the camera."""
    return 0.5 + 0.22 * np.sin(0.9 * x + 0.3) * np.cos(0.7 * z) + 0.15 * np.sin(0.35 * (x + z)) + (0.08 if on_wall else 0.0)


def render(eye, yaw, pitch, sub):
    """Luma image (float) averaged over sub x sub samples per pixel, and the pixel-centre view depth (-1 sky)."""
    fwd, U, V = basis(yaw, pitch)
    acc = np.zeros((H, W))
    for sy in range(sub):
        for sx in range(sub):
            x = 2.0 * (np.arange(W)[None, :] + (sx + 0.5) / sub) / W - 1.0
            y = 1.0 - 2.0 * (np.arange(H)[:, None] + (sy + 0.5) / sub) / H
            d = fwd[None, None, :] + x[..., None] * U + y[..., None] * V
            d = np.broadcast_to(d, (H, W, 3))
            t_ground = np.where(d[..., 1] < -1e-6, -eye[1] / np.minimum(d[..., 1], -1e-6), np.inf)
            t_wall = np.where(d[..., 2] < -1e-6, (-60.0 - eye[2]) / np.minimum(d[..., 2], -1e-6), np.inf)
            on_wall = t_wall < t_ground
            t = np.minimum(t_ground, t_wall)
            p = eye + d * np.where(np.isinf(t), 0, t)[..., None]
            lum = np.where(on_wall, texture(p[..., 0], p[..., 1], True), texture(p[..., 0], p[..., 2], False))
            acc += np.where(np.isinf(t), 0.75, lum)
    acc /= sub * sub
    d0 = fwd[None, None, :] + (2.0 * (np.arange(W)[None, :] + 0.5) / W - 1.0)[..., None] * U + (1.0 - 2.0 * (np.arange(H)[:, None] + 0.5) / H)[..., None] * V
    tg = np.where(d0[..., 1] < -1e-6, -eye[1] / np.minimum(d0[..., 1], -1e-6), np.inf)
    tw = np.where(d0[..., 2] < -1e-6, (-60.0 - eye[2]) / np.minimum(d0[..., 2], -1e-6), np.inf)
    t0 = np.minimum(tg, tw)
    z = np.where(np.isinf(t0), -1.0, t0)   # d0 . fwd == 1, so the ray parameter is already the view depth
    return acc, z


def write_depth(path, eye, yaw, pitch, z):
    fwd, U, V = basis(yaw, pitch)
    with open(path, "wb") as f:
        f.write(np.array([W, H], dtype=np.uint32).tobytes())
        f.write(np.concatenate([eye, fwd, U, V]).astype(np.float32).tobytes())
        f.write(z.astype(np.float32).tobytes())


def save_gray(path, lum):
    Image.fromarray((np.clip(lum, 0, 1) * 255 + 0.5).astype(np.uint8)).convert("RGB").save(path)


def make_sequence(folder, poses, noise, rng):
    for k, (eye, yaw, pitch) in enumerate(poses):
        live, z = render(eye, yaw, pitch, 1)
        ref, _ = render(eye, yaw, pitch, 4)
        if noise:
            live = live + rng.uniform(-noise, noise, live.shape)
        save_gray(os.path.join(folder, f"live_{k:04d}.png"), live)
        save_gray(os.path.join(folder, f"ref_{k:04d}.png"), ref)
        write_depth(os.path.join(folder, f"depth_{k:04d}.bin"), np.array(eye, dtype=float), yaw, pitch, z)


def measure(folder, extra=()):
    subprocess.run([sys.executable, os.path.join(HERE, "kararlilik.py"), folder, "--flip-her", "0", "--hareket-telafili", *extra],
                   check=True, capture_output=True)
    with open(os.path.join(folder, "kararlilik.json"), encoding="utf-8") as f:
        res = json.load(f)
    mc = res["kipirti_hareket_telafili"]
    return res["kipirti_piksel"]["ort"], mc["ort"], mc["maske_orani"], mc["referans_mc_degisim_ort"], mc["referans_ham_degisim_maskeli_ort"]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    rng = np.random.default_rng(7)
    still = [((0.0, 5.0, 0.0), 0.0, -0.3)] * 6
    moving = [((0.35 * k, 5.0 + 0.05 * k, -0.8 * k), 0.02 * k, -0.3 + 0.004 * k) for k in range(6)]
    amplitude = 0.02
    results = {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, poses, noise in (("durağan", still, 0.0), ("hareket", moving, 0.0), ("hareket+titreşim", moving, amplitude)):
            folder = os.path.join(tmp, name.replace("+", "_").replace("ğ", "g").replace("ş", "s").replace("ı", "i"))
            os.makedirs(folder)
            make_sequence(folder, poses, noise, rng)
            results[name] = measure(folder)
        # Reprojection broken (the previous frame's camera replaced by the current one): the measure must rise.
        folder = os.path.join(tmp, "hareket_bozuk")
        os.makedirs(folder)
        make_sequence(folder, moving, 0.0, rng)
        real_warp = kararlilik.warp_to_previous
        def identity_warp(cur, prev, tol):
            return real_warp(cur, cur, tol)
        kararlilik.warp_to_previous = identity_warp
        try:
            sys.argv = ["kararlilik.py", folder, "--flip-her", "0", "--hareket-telafili"]
            import contextlib, io
            with contextlib.redirect_stdout(io.StringIO()):
                kararlilik.main()
        finally:
            kararlilik.warp_to_previous = real_warp
        with open(os.path.join(folder, "kararlilik.json"), encoding="utf-8") as f:
            broken = json.load(f)["kipirti_hareket_telafili"]
    for name, (old, new, masked, ref_mc, ref_raw) in results.items():
        print(f"{name:18s} eski {old:.5f}  yeni {new:.5f}  maske {masked:.3f}  referans telafili {ref_mc:.5f} telafisiz {ref_raw:.5f}")
    print(f"{'bozuk izdusum':18s} referans telafili {broken['referans_mc_degisim_ort']:.5f} telafisiz {broken['referans_ham_degisim_maskeli_ort']:.5f}")
    # Mean |n_t - n_t-1| of two independent uniform(-A, A) values is 2A/3 (plus the 8-bit rounding).
    expected = 2.0 * amplitude / 3.0
    failures = []
    if results["durağan"][1] > 0.002:
        failures.append("durağan kamerada yeni ölçü ~0 değil")
    if results["hareket"][1] > 0.004:
        failures.append("gürültüsüz hareketli kamerada yeni ölçü ~0 değil")
    if results["hareket"][0] < 3 * results["hareket"][1]:
        failures.append("eski ölçü hareketli durumda yeniden izdüşümden belirgin yüksek değil")
    if not 0.7 * expected < results["hareket+titreşim"][1] < 1.3 * expected:
        failures.append(f"enjekte titreşim beklenen {expected:.5f} civarında ölçülmedi")
    if results["hareket"][3] > 0.2 * results["hareket"][4]:
        failures.append("izdüşüm referansın değişimini en az 5 kat azaltmadı")
    if broken["referans_mc_degisim_ort"] < 0.7 * broken["referans_ham_degisim_maskeli_ort"]:
        failures.append("bozuk izdüşüm referans değişimini yine de azalttı (sınama bozulmayı yakalayamıyor)")
    if results["hareket"][2] > 0.4:
        failures.append("maske oranı beklenenden yüksek")
    if failures:
        raise SystemExit("BAŞARISIZ: " + "; ".join(failures))
    print("geçti")


if __name__ == "__main__":
    main()
