"""Sahne 7 "Olcek": verileri indirir ve sahnenin okuyacagi bicime hazirlar (PLAN §6). Cikti assets/olcek/ (git disi),
kaynaklar ve lisanslar assets/olcek/KAYNAKLAR.md (git'te).

Her doku ayni bicimde yazilir: <ad>.tex = 16 baytlik baslik (b"OLTX", genislik, yukseklik, duzey sayisi; uint32) ve
ardindan en inceden en kabaya mip duzeyleri, RGBA8 (sRGB). Mip'ler dogrusal isikta ortalanir (sRGB'de ortalamak
koyulastirir). Sahne dosyayi oldugu gibi ekran kartina kopyalar: acilista cozme yok.

Yerel izgaralar sahildeki noktanin (olcek_bugun.h: kLat, kLon) cevresinde azimutal esit uzaklikli (aeqd) izdusumde,
dogu ve kuzey metre: sahnenin GGround eksenleriyle ayni.

  python tools/olcek_veri.py s2        # evin koyu: Sentinel-2 L2A gercek renk, 10 m, 8192^2 (82 km)
  python tools/olcek_veri.py bmng      # Dunya: Blue Marble NG Ekim, kuresel 21600x10800 + Ege 500 m yerel
  python tools/olcek_veri.py ay        # Ay: LRO renk (CGI Moon Kit)
  python tools/olcek_veri.py kiyi      # sahildeki noktanin kiyisi: Sentinel-2 su maskesinden isaretli uzaklik
  python tools/olcek_veri.py bulut     # bugunun gercek bulutlari: NASA GIBS (MODIS Terra ev cevresi, VIIRS dunya)
  python tools/olcek_veri.py yildiz    # yildizlar: HYG v4.1 (uzakligi bilinen her yildiz, gercek yeri, isigi, rengi)
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
# Sentinel-2: the home bay at 10 m. S2B 2025-09-23 (the clearest pass near "today": cloud < 0.01 % over the area), every
# tile that touches the square, found through the Earth Search STAC API (Element 84; COGs on AWS Open Data)
S2_DATE = "2025-09-23"


def s2_tiles(lat, lon, half_m):
    import json
    dlat = half_m / 111000.0 * 1.05
    dlon = half_m / (111000.0 * math.cos(math.radians(lat))) * 1.05
    body = {"collections": ["sentinel-2-l2a"], "bbox": [lon - dlon, lat - dlat, lon + dlon, lat + dlat],
            "datetime": f"{S2_DATE}T00:00:00Z/{S2_DATE}T23:59:59Z", "limit": 50}
    req = urllib.request.Request("https://earth-search.aws.element84.com/v1/search", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    feats = json.load(urllib.request.urlopen(req))["features"]
    # one scene per tile (the processing baselines can repeat a tile): the least nodata
    best = {}
    for f in feats:
        tile = f["properties"].get("s2:mgrs_tile") or f["id"].split("_")[1]
        nod = f["properties"].get("s2:nodata_pixel_percentage", 0.0)
        if tile not in best or nod < best[tile][0]:
            best[tile] = (nod, f["assets"]["visual"]["href"], f["id"])
    out = sorted(best.values(), key=lambda t: t[0])
    for nod, href, fid in out:
        print(f"  {fid}: bos %{nod:.1f}")
    return [href for _, href, _ in out]


def s2(size=8192, res=10.0):
    import rasterio
    from rasterio.transform import from_origin
    from rasterio.warp import Resampling, reproject

    lat, lon = today_site()
    half = size * res / 2
    dst_t = from_origin(-half, half, res, res)
    dst = np.zeros((3, size, size), np.uint8)
    valid = np.zeros((size, size), np.uint8)
    for url in s2_tiles(lat, lon, half):
        with rasterio.open("/vsicurl/" + url) as ds:
            src = ds.read()
            band = np.zeros((3, size, size), np.uint8)
            reproject(src, band, src_transform=ds.transform, src_crs=ds.crs, dst_transform=dst_t, dst_crs=local_crs(lat, lon),
                      resampling=Resampling.lanczos, src_nodata=0, dst_nodata=0)
            m = (band.max(axis=0) > 0) & (valid == 0)
            dst[:, m] = band[:, m]
            valid[m] = 1
            print(f"  {url.split('/')[-2]}: kapsanan %{100 * valid.mean():.1f}")
    # TCI is linear in reflectance: DN 255 = 0.2 (L2A, B04/B03/B02 scaled 0..2000 of 10000). Stored as sRGB of
    # reflectance / 0.25 like every other map here (the scene multiplies by 0.25): more levels for the dark sea
    refl = dst.astype(np.float32) / 255.0 * 0.2
    enc = lin_to_srgb(np.moveaxis(refl / 0.25, 0, -1))
    rgba = np.concatenate([enc, (valid * 255)[..., None]], axis=2)
    write_tex(os.path.join(OUT, "s2_ev.tex"), rgba)
    from PIL import Image
    Image.fromarray(np.moveaxis(dst, 0, -1)).resize((1024, 1024), Image.BOX).save(os.path.join(OUT, "s2_ev_onizleme.png"))


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
    zonal(im)
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


def zonal(im):
    """The world's mean colour along each latitude (linear light): the Earth spinning many times in the eye's shutter."""
    lin = srgb_to_lin(im)
    rows = lin.mean(axis=1)                                   # H x 3
    col = lin_to_srgb(rows)[:, None, :].repeat(4, axis=1)     # H x 4 x 3
    rgba = np.concatenate([col, np.full(col.shape[:2] + (1,), 255, np.uint8)], axis=2)
    write_tex(os.path.join(OUT, "bmng_kusak.tex"), rgba)


# ---------------------------------------------------------------------------------------------------------------------
# The Moon: LRO LROC WAC colour mosaic (NASA SVS 4720, CGI Moon Kit; public domain), 8192x4096
def ay():
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None
    path = fetch("https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_8k.tif", "lroc_color_poles_8k.tif")
    im = np.asarray(Image.open(path).convert("RGB"))
    rgba = np.concatenate([im, np.full(im.shape[:2] + (1,), 255, np.uint8)], axis=2)
    write_tex(os.path.join(OUT, "ay.tex"), rgba)


# ---------------------------------------------------------------------------------------------------------------------
# The shore at the beach point: the sea's signed distance (m, + over the water) from the Sentinel-2 water mask, 4 km
# square around the beach point at 2 m (smooth enough to interpolate). The scene draws the beach's swash where it crosses
# zero, so the procedural beach lies on the real coastline. Written as a float32 file: "OLSD", size, metres per texel,
# then the values (rows from the north).
def kiyi(half=2048.0, res=2.0):
    from scipy import ndimage
    with open(os.path.join(OUT, "s2_ev.tex"), "rb") as f:
        assert f.read(4) == b"OLTX"
        w, h, _ = np.frombuffer(f.read(12), np.uint32)
        img = np.frombuffer(f.read(int(w) * int(h) * 4), np.uint8).reshape(int(h), int(w), 4)
    s2res = 10.0
    c = int(w) // 2
    n = int(half / s2res) + 8
    crop = img[c - n:c + n, c - n:c + n]
    refl = srgb_to_lin(crop[..., :3]) * 0.25
    r, g, b = refl[..., 0], refl[..., 1], refl[..., 2]
    # water: dark in red (sand and towns are bright there); shallow water over sand is brighter in green and blue
    water = (r < 0.045) & (b >= r * 0.85) & (crop[..., 3] > 0)
    water = ndimage.binary_opening(water, iterations=1)
    water = ndimage.binary_closing(water, iterations=1)
    # keep the sea (the largest water body touching the square), not ponds
    lab, nl = ndimage.label(water)
    if nl > 1:
        sizes = ndimage.sum(water, lab, range(1, nl + 1))
        water = lab == (1 + int(np.argmax(sizes)))
    # signed distance at 10 m, then upsampled to `res` (an SDF interpolates well), + over the water
    d_out = ndimage.distance_transform_edt(~water) * s2res
    d_in = ndimage.distance_transform_edt(water) * s2res
    sd = np.where(water, d_in - 0.5 * s2res, -(d_out - 0.5 * s2res)).astype(np.float32)
    sd = ndimage.gaussian_filter(sd, 1.0)
    k = s2res / res
    m = int(round(2 * half / res))
    # sample: output pixel (i, j) centre at x = -half + (j + 0.5) res, y = half - (i + 0.5) res; crop pixel centre at
    # x = (cj - n + 0.5) s2res, with cj the column in the crop
    jj = (np.arange(m) + 0.5) * res - half
    cols = jj / s2res + n - 0.5
    rows = (-jj) / s2res + n - 0.5
    out = ndimage.map_coordinates(sd, np.meshgrid(rows, cols, indexing="ij"), order=3, mode="nearest").astype(np.float32)
    # the beach point: how far from the water it sits on the map (the scene puts it at the swash's mean reach)
    centre = out[m // 2, m // 2]
    gy, gx = np.gradient(out, res)
    nx, ny = gx[m // 2, m // 2], -gy[m // 2, m // 2]   # towards the sea, east and north
    ang = math.degrees(math.atan2(ny, nx))
    print(f"  kiyi: sahildeki nokta suya {-centre:.1f} m (+ karada), denize dogru yon {ang:.0f} derece (doguya gore)")
    with open(os.path.join(OUT, "kiyi.sdf"), "wb") as f:
        f.write(b"OLSD")
        f.write(np.array([m], np.uint32).tobytes())
        f.write(np.array([res], np.float32).tobytes())
        f.write(out.tobytes())
    from PIL import Image
    v = np.clip(0.5 + out / 200.0, 0, 1)
    Image.fromarray((v * 255).astype(np.uint8)).resize((1024, 1024)).save(os.path.join(OUT, "kiyi_onizleme.png"))
    print(f"  yazildi assets/olcek/kiyi.sdf: {m}x{m}, {res} m")


# ---------------------------------------------------------------------------------------------------------------------
# Clouds: today's (5 October 2026) real sky from NASA GIBS (Worldview's imagery: MODIS Terra and VIIRS NOAA-20
# corrected reflectance, true colour). A cloud is what the satellite sees brighter and whiter than the cloudless Blue
# Marble at the same place: alpha from that difference, colour the satellite's own. Around home MODIS Terra of today
# (its pass ~09:00 UTC, an hour before noon at the beach); the world from VIIRS NOAA-20 (a whole day without gaps:
# today's when it is complete, otherwise yesterday's).
GIBS = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi?SERVICE=WMS&REQUEST=GetMap&VERSION=1.3.0&CRS=EPSG:4326"


def gibs(layer, date, bbox, w, h, name):
    lat0, lon0, lat1, lon1 = bbox
    url = f"{GIBS}&LAYERS={layer}&BBOX={lat0},{lon0},{lat1},{lon1}&WIDTH={w}&HEIGHT={h}&FORMAT=image/jpeg&TIME={date}"
    path = fetch(url, name)
    from PIL import Image
    return np.asarray(Image.open(path).convert("RGB"))


def cloud_alpha(sat, base):
    """sat, base: HxWx3 uint8 (sRGB) on the same grid. Returns alpha 0..1."""
    ls = srgb_to_lin(sat)
    lb = srgb_to_lin(base)
    Ys = ls @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    Yb = lb @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    mx, mn = ls.max(axis=2), ls.min(axis=2)
    satur = (mx - mn) / np.maximum(mx, 1e-4)
    a = np.clip((Ys - Yb - 0.03) / 0.30, 0.0, 1.0) * np.clip((0.45 - satur) / 0.25, 0.0, 1.0)
    a[sat.max(axis=2) < 4] = 0.0   # no data
    return a


def bulut(date="2026-10-05", world_date=None):
    from PIL import Image
    import rasterio
    from rasterio.transform import from_origin
    from rasterio.warp import Resampling, reproject

    Image.MAX_IMAGE_PIXELS = None
    lat, lon = today_site()
    # ---- around home: MODIS Terra of the day, 2048 km square at 500 m (the region map's grid)
    bbox = (lat - 11.0, lon - 15.0, lat + 11.0, lon + 15.0)
    W, H = 8000, 5867
    sat = gibs("MODIS_Terra_CorrectedReflectance_TrueColor", date, bbox, W, H, f"terra_{date}_ev.jpg")
    c1 = np.asarray(Image.open(fetch(BMNG + "world.200410.3x21600x21600.C1.jpg", "world.200410.3x21600x21600.C1.jpg")).convert("RGB"))
    size, res = 4096, 500.0
    half = size * res / 2
    dst_t = from_origin(-half, half, res, res)
    crs = local_crs(lat, lon)
    src_sat = np.moveaxis(sat, -1, 0)
    sat_l = np.zeros((3, size, size), np.uint8)
    reproject(src_sat, sat_l, src_transform=from_origin(bbox[1], bbox[2], (bbox[3] - bbox[1]) / W, (bbox[2] - bbox[0]) / H),
              src_crs="EPSG:4326", dst_transform=dst_t, dst_crs=crs, resampling=Resampling.bilinear)
    base_l = np.zeros((3, size, size), np.uint8)
    reproject(np.moveaxis(c1, -1, 0), base_l, src_transform=from_origin(0.0, 90.0, 90.0 / 21600, 90.0 / 21600), src_crs="EPSG:4326",
              dst_transform=dst_t, dst_crs=crs, resampling=Resampling.average)
    sat_l, base_l = np.moveaxis(sat_l, 0, -1), np.moveaxis(base_l, 0, -1)
    a = cloud_alpha(sat_l, base_l)
    # the sky over the beach was clear on the day (MODIS Terra's picture); what the difference calls a cloud within
    # ~15 km of it is Candarli's salt pans, bright and white on the ground
    yy, xx = np.mgrid[0:size, 0:size]
    rr = np.hypot((xx + 0.5) * res - half, half - (yy + 0.5) * res)
    a = a * np.clip((rr - 12e3) / 8e3, 0.0, 1.0)
    rgba = np.concatenate([sat_l, np.round(a * 255).astype(np.uint8)[..., None]], axis=2)
    write_tex(os.path.join(OUT, "bulut_ege.tex"), rgba)
    Image.fromarray((a * 255).astype(np.uint8)).resize((1024, 1024), Image.BOX).save(os.path.join(OUT, "bulut_ege_alfa.png"))
    print(f"  ev cevresi bulut orani %{100 * a.mean():.1f}, sahildeki noktada {a[size // 2, size // 2]:.2f}")
    del c1
    # ---- the world: VIIRS NOAA-20, 8192 x 4096
    wd = world_date or date
    sat = gibs("VIIRS_NOAA20_CorrectedReflectance_TrueColor", wd, (-90, -180, 90, 180), 8192, 4096, f"viirs_{wd}_dunya.jpg")
    nodata = (sat.max(axis=2) < 4).mean()
    if nodata > 0.25 and world_date is None:
        print(f"  {wd}: dunyanin %{100 * nodata:.0f}'i bos (gun tamamlanmadi), bir onceki gun")
        import datetime
        wd = (datetime.date.fromisoformat(date) - datetime.timedelta(days=1)).isoformat()
        sat = gibs("VIIRS_NOAA20_CorrectedReflectance_TrueColor", wd, (-90, -180, 90, 180), 8192, 4096, f"viirs_{wd}_dunya.jpg")
    world = np.asarray(Image.open(fetch(BMNG + "world.200410.3x21600x10800.jpg", "world.200410.3x21600x10800.jpg")).convert("RGB"))
    base = np.asarray(Image.fromarray(world).resize((8192, 4096), Image.BOX))
    a = cloud_alpha(sat, base)
    rgba = np.concatenate([sat, np.round(a * 255).astype(np.uint8)[..., None]], axis=2)
    write_tex(os.path.join(OUT, "bulut_dunya.tex"), rgba)
    Image.fromarray((a * 255).astype(np.uint8)).resize((2048, 1024), Image.BOX).save(os.path.join(OUT, "bulut_dunya_alfa.png"))
    print(f"  dunya ({wd}) bulut orani %{100 * a.mean():.1f}")


# ---------------------------------------------------------------------------------------------------------------------
# Stars: the HYG database v4.1 (Hipparcos, Yale Bright Star, Gliese; astronexus, CC BY-SA 4.0): every star with a
# distance, its place from the Sun (ICRS, pc -> m), its luminosity (L_sun, visual) and its colour from B-V. Written as
# "OLST", count, then per star x, y, z (float64, m) and luminosity, r, g, b (float32).
def yildiz():
    import csv
    path = fetch("https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv", "hygdata_v41.csv")
    pc = 3.0856775814913673e16
    rows = []
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            try:
                d = float(r["dist"])
                if d <= 0.0 or d >= 99999.0:
                    continue
                x, y, z = float(r["x"]), float(r["y"]), float(r["z"])
                absmag = float(r["absmag"])
            except (ValueError, KeyError):
                continue
            ci = r.get("ci", "")
            try:
                bv = float(ci)
            except ValueError:
                bv = 0.65
            bv = min(max(bv, -0.4), 2.0)
            T = 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62))   # Ballesteros 2012
            lum = 10.0 ** (-0.4 * (absmag - 4.83))
            rows.append((x * pc, y * pc, z * pc, lum, T))
    rows.sort(key=lambda t: -t[3])
    n = len(rows)

    def bb(T):
        out = []
        for nm in (610.0, 550.0, 465.0):
            l = nm * 1e-9
            out.append(1.0 / (l ** 5 * (math.exp(1.4387769e-2 / (l * T)) - 1.0)))
        y = 0.2126 * out[0] + 0.7152 * out[1] + 0.0722 * out[2]
        return [v / y for v in out]

    pos = np.array([[r[0], r[1], r[2]] for r in rows], np.float64)
    att = np.array([[r[3]] + bb(r[4]) for r in rows], np.float32)
    with open(os.path.join(OUT, "yildizlar.bin"), "wb") as f:
        f.write(b"OLST")
        f.write(np.array([n], np.uint32).tobytes())
        f.write(pos.tobytes())
        f.write(att.tobytes())
    near = sum(1 for r in rows if (r[0] ** 2 + r[1] ** 2 + r[2] ** 2) ** 0.5 < 10 * pc)
    print(f"  yazildi assets/olcek/yildizlar.bin: {n} yildiz ({near} tanesi 10 pc icinde)")


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "hepsi"
    os.makedirs(OUT, exist_ok=True)
    if what in ("s2", "hepsi"):
        s2()
    if what in ("bmng", "hepsi"):
        bmng()
    if what in ("ay", "hepsi"):
        ay()
    if what in ("kiyi", "hepsi"):
        kiyi()
    if what in ("bulut", "hepsi"):
        bulut()
    if what in ("yildiz", "hepsi"):
        yildiz()
    if what == "kusak":
        from PIL import Image
        Image.MAX_IMAGE_PIXELS = None
        zonal(np.asarray(Image.open(fetch(BMNG + "world.200410.3x21600x10800.jpg", "world.200410.3x21600x10800.jpg")).convert("RGB")))
