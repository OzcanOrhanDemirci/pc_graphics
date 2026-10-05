"""Sahne 7 "Olcek": verileri indirir ve sahnenin okuyacagi bicime hazirlar (PLAN §6). Cikti assets/olcek/ (git disi),
kaynaklar ve lisanslar assets/olcek/KAYNAKLAR.md (git'te).

Her doku ayni bicimde yazilir: <ad>.tex = 16 baytlik baslik (b"OLTX", genislik, yukseklik, duzey sayisi; uint32) ve
ardindan en inceden en kabaya mip duzeyleri, RGBA8 (sRGB). Mip'ler dogrusal isikta ortalanir (sRGB'de ortalamak
koyulastirir). Sahne dosyayi oldugu gibi ekran kartina kopyalar: acilista cozme yok.

Yerel izgaralar sahildeki noktanin (olcek_bugun.h: kLat, kLon) cevresinde azimutal esit uzaklikli (aeqd) izdusumde,
dogu ve kuzey metre: sahnenin GGround eksenleriyle ayni.

  python tools/olcek_veri.py s2        # Izmir Korfezi: Sentinel-2 L2A gercek renk, 10 m, 8192^2 (82 km)
  python tools/olcek_veri.py bmng      # Dunya: Blue Marble NG Ekim, kuresel 21600x10800 + Ege 500 m yerel
  python tools/olcek_veri.py ay        # Ay: LRO renk (CGI Moon Kit)
  python tools/olcek_veri.py hepsi
"""
import math
import os
import re
import sys
import urllib.request

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "olcek")
DL = os.path.join(OUT, "_dl")


def today_site():
    h = open(os.path.join(ROOT, "src", "scenes", "olcek", "olcek_bugun.h"), encoding="utf-8").read()
    m = re.search(r"kLat = ([-\d.]+), kLon = ([-\d.]+)", h)
    return float(m.group(1)), float(m.group(2))


def srgb_to_lin(a):
    a = a.astype(np.float32) / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(a):
    a = np.clip(a, 0.0, 1.0)
    return np.round(255.0 * np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1 / 2.4) - 0.055)).astype(np.uint8)


def write_tex(path, rgba):
    """rgba: HxWx4 uint8 (sRGB colour, linear alpha). Writes it and its mips (linear-light box filter)."""
    h, w = rgba.shape[:2]
    levels = [rgba]
    lin = np.concatenate([srgb_to_lin(rgba[..., :3]), rgba[..., 3:4].astype(np.float32) / 255.0], axis=2)
    while w > 1 or h > 1:
        nh, nw = max(1, h // 2), max(1, w // 2)
        lin = lin[: nh * 2, : nw * 2]
        if h > 1 and w > 1:
            lin = 0.25 * (lin[0::2, 0::2] + lin[1::2, 0::2] + lin[0::2, 1::2] + lin[1::2, 1::2])
        elif h > 1:
            lin = 0.5 * (lin[0::2] + lin[1::2])
        else:
            lin = 0.5 * (lin[:, 0::2] + lin[:, 1::2])
        h, w = nh, nw
        lv = np.concatenate([lin_to_srgb(lin[..., :3]), np.round(np.clip(lin[..., 3:4], 0, 1) * 255).astype(np.uint8)], axis=2)
        levels.append(lv)
    with open(path, "wb") as f:
        f.write(b"OLTX")
        f.write(np.array([rgba.shape[1], rgba.shape[0], len(levels)], dtype=np.uint32).tobytes())
        for lv in levels:
            f.write(np.ascontiguousarray(lv).tobytes())
    print(f"  yazildi {os.path.relpath(path, ROOT)}: {rgba.shape[1]}x{rgba.shape[0]}, {len(levels)} duzey, "
          f"{os.path.getsize(path) / 1e6:.0f} MB")


def fetch(url, name):
    os.makedirs(DL, exist_ok=True)
    path = os.path.join(DL, name)
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path
    print("  indiriliyor", url)
    req = urllib.request.Request(url, headers={"User-Agent": "pc_graphics olcek_veri.py"})
    with urllib.request.urlopen(req) as r, open(path + ".part", "wb") as f:
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            f.write(b)
    os.replace(path + ".part", path)
    return path


def local_crs(lat, lon):
    return f"+proj=aeqd +lat_0={lat} +lon_0={lon} +x_0=0 +y_0=0 +ellps=WGS84 +units=m +no_defs"


# ---------------------------------------------------------------------------------------------------------------------
# Sentinel-2: the gulf at 10 m. S2B 2025-09-23, the clearest scene near "today" (cloud 0.008 %), tiles 35SMC + 35SNC
S2_TILES = [
    "https://sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/35/S/MC/2025/9/S2B_35SMC_20250923_0_L2A/TCI.tif",
    "https://sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/35/S/NC/2025/9/S2B_35SNC_20250923_0_L2A/TCI.tif",
]


def s2(size=8192, res=10.0):
    import rasterio
    from rasterio.transform import from_origin
    from rasterio.warp import Resampling, reproject

    lat, lon = today_site()
    half = size * res / 2
    dst_t = from_origin(-half, half, res, res)
    dst = np.zeros((3, size, size), np.uint8)
    valid = np.zeros((size, size), np.uint8)
    for url in S2_TILES:
        with rasterio.open("/vsicurl/" + url) as ds:
            src = ds.read()
            band = np.zeros((3, size, size), np.uint8)
            reproject(src, band, src_transform=ds.transform, src_crs=ds.crs, dst_transform=dst_t, dst_crs=local_crs(lat, lon),
                      resampling=Resampling.lanczos, src_nodata=0, dst_nodata=0)
            m = (band.max(axis=0) > 0) & (valid == 0)
            dst[:, m] = band[:, m]
            valid[m] = 1
            print(f"  {url.split('/')[-2]}: kapsanan %{100 * valid.mean():.1f}")
    rgba = np.concatenate([np.moveaxis(dst, 0, -1), (valid * 255)[..., None]], axis=2)
    write_tex(os.path.join(OUT, "s2_izmir.tex"), rgba)
    from PIL import Image
    Image.fromarray(np.moveaxis(dst, 0, -1)).resize((1024, 1024), Image.BOX).save(os.path.join(OUT, "s2_izmir_onizleme.png"))


# ---------------------------------------------------------------------------------------------------------------------
# Blue Marble Next Generation, October 2004 (Stockli et al., NASA Earth Observatory; public domain): the Earth without
# clouds. The world at 21600x10800 (~1.85 km), and around home the 500 m tile cut to a local grid (2048 km square).
BMNG = "https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-base/october/"


def bmng(size=4096, res=500.0):
    from PIL import Image
    import rasterio
    from rasterio.transform import from_origin
    from rasterio.warp import Resampling, reproject

    Image.MAX_IMAGE_PIXELS = None
    path = fetch(BMNG + "world.200410.3x21600x10800.jpg", "world.200410.3x21600x10800.jpg")
    im = np.asarray(Image.open(path).convert("RGB"))
    rgba = np.concatenate([im, np.full(im.shape[:2] + (1,), 255, np.uint8)], axis=2)
    write_tex(os.path.join(OUT, "bmng_dunya.tex"), rgba)
    Image.fromarray(im).resize((2048, 1024), Image.BOX).save(os.path.join(OUT, "bmng_dunya_onizleme.png"))
    del im, rgba

    lat, lon = today_site()
    path = fetch(BMNG + "world.200410.3x21600x21600.C1.jpg", "world.200410.3x21600x21600.C1.jpg")   # lon 0..90 E, lat 90..0 N
    src = np.moveaxis(np.asarray(Image.open(path).convert("RGB")), -1, 0)
    src_t = from_origin(0.0, 90.0, 90.0 / 21600, 90.0 / 21600)
    half = size * res / 2
    dst = np.zeros((3, size, size), np.uint8)
    reproject(src, dst, src_transform=src_t, src_crs="EPSG:4326", dst_transform=from_origin(-half, half, res, res),
              dst_crs=local_crs(lat, lon), resampling=Resampling.lanczos)
    rgba = np.concatenate([np.moveaxis(dst, 0, -1), np.full((size, size, 1), 255, np.uint8)], axis=2)
    write_tex(os.path.join(OUT, "bmng_ege.tex"), rgba)
    Image.fromarray(np.moveaxis(dst, 0, -1)).resize((1024, 1024), Image.BOX).save(os.path.join(OUT, "bmng_ege_onizleme.png"))


# ---------------------------------------------------------------------------------------------------------------------
# The Moon: LRO LROC WAC colour mosaic (NASA SVS 4720, CGI Moon Kit; public domain), 8192x4096
def ay():
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None
    path = fetch("https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_8k.tif", "lroc_color_poles_8k.tif")
    im = np.asarray(Image.open(path).convert("RGB"))
    rgba = np.concatenate([im, np.full(im.shape[:2] + (1,), 255, np.uint8)], axis=2)
    write_tex(os.path.join(OUT, "ay.tex"), rgba)


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "hepsi"
    os.makedirs(OUT, exist_ok=True)
    if what in ("s2", "hepsi"):
        s2()
    if what in ("bmng", "hepsi"):
        bmng()
    if what in ("ay", "hepsi"):
        ay()
