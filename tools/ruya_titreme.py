"""Rüya hareket titremesi: bir rotanın canlı karelerini aynı gölgelendirmenin kaba kuvvet referansına karşı ölçer.

  python tools/ruya_titreme.py --out KLASOR [--exe build/pc_graphics.exe] [--frames 24] [--ref-spp 128]
                               [--live-args "..."] -- ROTA_SECENEKLERI

Canlı: sahnenin ayarı (12 örnek, bounces 0). Referans: aynı rota, --spp REF --reference (her örnek kendi yüzeyini
kendi izinde gölgeler). Kareler KLASOR/live_XXXX.png ve ref_XXXX.png olur, EXR'ler silinir. Canlı koşu ayrıca
--dump-depth ile kare başına derinlik ve kamerayı depth_XXXX.bin yazar; sonra tools/kararlilik.py --isi
--hareket-telafili çalışır; ısı haritası 1600 genişlikte JPG'ye de çevrilir.
"""
import argparse
import os
import shlex
import shutil
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))


def render(exe, out_dir, frames, extra, route):
    raw = out_dir + "_raw"
    shutil.rmtree(raw, ignore_errors=True)
    command = [exe, "--scene", "ruya", "--headless", "--frames", str(frames), "--warmup", "3", "--dump-dir", raw] + extra + route
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"cizim basarisiz ({result.returncode}): {' '.join(command)}\n{result.stderr}")
    return raw


def collect(raw, out, prefix):
    for name in sorted(os.listdir(raw)):
        path = os.path.join(raw, name)
        if name.endswith(".png"):
            os.replace(path, os.path.join(out, prefix + name[len("frame_"):]))
        elif name.startswith("depth_") and name.endswith(".bin"):
            os.replace(path, os.path.join(out, name))
        else:
            os.remove(path)
    os.rmdir(raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--exe", default=os.path.join(HERE, "..", "build", "pc_graphics.exe"))
    ap.add_argument("--frames", type=int, default=24)
    ap.add_argument("--ref-spp", type=int, default=128)
    ap.add_argument("--live-args", default="", help="yalnız canlı koşuya eklenen secenekler (A/B icin)")
    ap.add_argument("--keep-ref", help="referansi bu klasorden kopyala (ayni rota, ayni kod)")
    ap.add_argument("route", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)
    route = a.route[1:] if a.route[:1] == ["--"] else a.route
    os.makedirs(a.out, exist_ok=True)
    for name in os.listdir(a.out):
        if (name.startswith(("live_", "ref_")) and name.endswith(".png")) or (name.startswith("depth_") and name.endswith(".bin")):
            os.remove(os.path.join(a.out, name))
    collect(render(a.exe, os.path.join(a.out, "live"), a.frames, ["--dump-depth"] + shlex.split(a.live_args), route), a.out, "live_")
    if a.keep_ref:
        for name in sorted(os.listdir(a.keep_ref)):
            if name.startswith("ref_") and name.endswith(".png"):
                shutil.copyfile(os.path.join(a.keep_ref, name), os.path.join(a.out, name))
    else:
        collect(render(a.exe, os.path.join(a.out, "ref"), a.frames, ["--spp", str(a.ref_spp), "--reference"], route), a.out, "ref_")
    with open(os.path.join(a.out, "komut.txt"), "w", encoding="utf-8") as f:
        f.write(" ".join(sys.argv) + "\n")
    subprocess.run([sys.executable, os.path.join(HERE, "kararlilik.py"), a.out, "--isi", "--hareket-telafili"], check=True)
    heat = Image.open(os.path.join(a.out, "kipirti_isi.png")).convert("RGB")
    heat.resize((1600, round(1600 * heat.height / heat.width)), Image.LANCZOS).save(os.path.join(a.out, "isi.jpg"), quality=90)


if __name__ == "__main__":
    main()
