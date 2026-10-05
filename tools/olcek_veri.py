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
  python tools/olcek_veri.py galaksi   # galaksiler: 2MASS Redshift Survey (44.600 galaksi, kirmiziya kaymadan uzaklik)
  python tools/olcek_veri.py ag        # kozmik ag: Zel'dovich yaklasimiyla istatistiksel bir ornek (256^3, 600 Mpc)
  python tools/olcek_veri.py cie       # CIE 1931 2 derece renk eslestirme fonksiyonlari (Hiz: isigin her hizdaki rengi)
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
    # ---- the world: VIIRS NOAA-20, 8192 x 4096. Its daily mosaic carries the sunglint of each swath (broad, smooth, grey
    # bands over the tropical sea) that the difference against Blue Marble takes for cloud. So the cloud is also asked
    # of a retrieval: VIIRS NOAA-20's cloud optical thickness, drawn only where its cloud mask found cloud (glint-free).
    # The product comes ~2 days late: the most recent day that has both is used (re-running later moves it on).
    import datetime
    wd = world_date or date
    for back in range(5):
        cot_url = (f"{GIBS}&LAYERS=VIIRS_NOAA20_Cloud_Optical_Thickness&BBOX=-90,-180,90,180&WIDTH=8192&HEIGHT=4096"
                   f"&FORMAT=image/png&TRANSPARENT=TRUE&TIME={wd}")
        cot = np.asarray(Image.open(fetch(cot_url, f"viirs_cot_{wd}_dunya.png")).convert("RGBA"))
        sat = gibs("VIIRS_NOAA20_CorrectedReflectance_TrueColor", wd, (-90, -180, 90, 180), 8192, 4096, f"viirs_{wd}_dunya.jpg")
        nodata = (sat.max(axis=2) < 4).mean()
        cover = (cot[..., 3] > 0).mean()
        if world_date is not None or (nodata < 0.25 and cover > 0.2):
            break
        print(f"  {wd}: dunya %{100 * nodata:.0f} bos, bulut urunu %{100 * cover:.0f}: bir onceki gun")
        # an unfinished day is not kept: a later run fetches it again
        for name in (f"viirs_cot_{wd}_dunya.png", f"viirs_{wd}_dunya.jpg"):
            if os.path.exists(os.path.join(DL, name)):
                os.remove(os.path.join(DL, name))
        wd = (datetime.date.fromisoformat(wd) - datetime.timedelta(days=1)).isoformat()
    from scipy.ndimage import maximum_filter, uniform_filter
    mask = uniform_filter(maximum_filter((cot[..., 3] > 0).astype(np.float32), size=3), size=3)   # a pixel's margin, soft
    world = np.asarray(Image.open(fetch(BMNG + "world.200410.3x21600x10800.jpg", "world.200410.3x21600x10800.jpg")).convert("RGB"))
    base = np.asarray(Image.fromarray(world).resize((8192, 4096), Image.BOX))
    a = cloud_alpha(sat, base) * np.clip(mask * 1.5, 0.0, 1.0)
    rgba = np.concatenate([sat, np.round(a * 255).astype(np.uint8)[..., None]], axis=2)
    write_tex(os.path.join(OUT, "bulut_dunya.tex"), rgba)
    Image.fromarray((a * 255).astype(np.uint8)).resize((2048, 1024), Image.BOX).save(os.path.join(OUT, "bulut_dunya_alfa.png"))
    print(f"  dunya ({wd}) bulut orani %{100 * a.mean():.1f}")


# ---------------------------------------------------------------------------------------------------------------------
# Stars: the HYG database v4.1 (Hipparcos, Yale Bright Star, Gliese; astronexus, CC BY-SA 4.0): every star with a
# distance, its place from the Sun (ICRS, pc -> m), its luminosity (L_sun, visual) and its colour from B-V. Written as
# "OLS2", count, then per star x, y, z (float64, m) and luminosity, r, g, b, temperature (float32; the temperature: the
# second option "Hiz" shifts it by the Doppler factor, docs/olcek/hiz/PLAN.md). The scene still reads the older "OLST"
# (no temperature: it is then recovered from the colour).
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
    att = np.array([[r[3]] + bb(r[4]) + [r[4]] for r in rows], np.float32)
    with open(os.path.join(OUT, "yildizlar.bin"), "wb") as f:
        f.write(b"OLS2")
        f.write(np.array([n], np.uint32).tobytes())
        f.write(pos.tobytes())
        f.write(att.tobytes())
    near = sum(1 for r in rows if (r[0] ** 2 + r[1] ** 2 + r[2] ** 2) ** 0.5 < 10 * pc)
    print(f"  yazildi assets/olcek/yildizlar.bin: {n} yildiz ({near} tanesi 10 pc icinde)")


# ---------------------------------------------------------------------------------------------------------------------
# The CIE 1931 2-degree colour matching functions (CIE 018:2019 Table 6; DOI 10.25039/CIE.DS.xvudnb9b; CC BY-SA 4.0):
# 360-830 nm at 1 nm, "lambda,xbar,ybar,zbar" per line, no header. The second option "Hiz" builds the colour and the
# visible share of a blackbody at any temperature from it (src/scenes/olcek/olcek_hiz_goz.cpp), so the Doppler-shifted
# light of every star is exact. Copied unmodified, its checksum pinned.
CIE_URL = "https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv"
CIE_SHA256 = "fa663e3535a7e0763a745993a1f0a192eb0275ac46ad2d1befd7626841e713c1"


def cie():
    import hashlib
    import shutil
    path = fetch(CIE_URL, "CIE_xyz_1931_2deg.csv")
    h = hashlib.sha256(open(path, "rb").read()).hexdigest()
    if h != CIE_SHA256:
        raise SystemExit(f"  CIE tablosunun ozeti beklenen degil: {h}")
    shutil.copyfile(path, os.path.join(OUT, "cie1931_2deg.csv"))
    rows = sum(1 for line in open(path) if line.strip())
    print(f"  yazildi assets/olcek/cie1931_2deg.csv: {rows} satir (360-830 nm), sha256 dogrulandi")


# ---------------------------------------------------------------------------------------------------------------------
# The Milky Way's diffuse light for the second option's eye (PLAN §3.1): ESO/S. Brunier's whole-sky panorama (eso0932a,
# CC BY 4.0; ESO's "large" JPEG, 6000 x 3000), equirectangular in galactic coordinates: the centre at the middle,
# longitude growing to the left, latitude up. Its stars are taken out by a median 9 pixels wide (0.54 degrees: the
# catalogue draws them as points, no doubles), the ground's sky glow by the floor at high latitudes; what is left is the
# unresolved starlight and the nebulae. Its colour is kept, and beside it a colour temperature (McCamy, from the
# chromaticity): the eye's Doppler shift scales the colour by the blackbody's at that temperature. Its scale: the
# brightest few degrees (the Sagittarius star cloud) at 20.5 V mag per square arcsecond, in the eye's units (W/m^2/sr of
# a Sun-like light: V = -26.74 is 1361 W/m^2). Written as "OLSK": width, height, then float16 RGBA per texel (linear RGB
# in those units; A the temperature / 10^4 K), the top row first (galactic north).
ESO_URL = "https://www.eso.org/public/archives/images/large/eso0932a.jpg"
ESO_SHA256 = "60400c92c54b7c1bd12299c69e83b16e5b6256e7dabacc478c021758ecd28179"


def samanyolu(W=2048, mu=20.5):
    import hashlib
    from PIL import Image
    from scipy import ndimage
    Image.MAX_IMAGE_PIXELS = None
    path = fetch(ESO_URL, "eso0932a.jpg")
    h = hashlib.sha256(open(path, "rb").read()).hexdigest()
    if h != ESO_SHA256:
        raise SystemExit(f"  ESO panoramasinin ozeti beklenen degil: {h}")
    im = np.asarray(Image.open(path).convert("RGB"))
    lin = srgb_to_lin(im)
    print(f"  panorama {im.shape[1]}x{im.shape[0]}: yildizlar medyanla siliniyor")
    med = np.stack([ndimage.median_filter(lin[..., c], size=9, mode="wrap") for c in range(3)], axis=2)
    # what the median leaves of the brightest stars and the planet (their halos wider than it): clipped at three times the
    # two-degree median of their surroundings (the Milky Way's own clouds seldom reach that)
    wide = np.stack([ndimage.median_filter(med[::3, ::3, c], size=11, mode="wrap") for c in range(3)], axis=2)
    wide = np.repeat(np.repeat(wide, 3, axis=0), 3, axis=1)[: med.shape[0], : med.shape[1]]
    med = np.minimum(med, 3.0 * wide + 0.004)
    H = med.shape[0]
    b = 90.0 - (np.arange(H) + 0.5) * 180.0 / H
    floor = np.percentile(med[np.abs(b) > 40.0].reshape(-1, 3), 20, axis=0)
    med = np.clip(med - floor, 0.0, None)
    print(f"  yer gok parlamasi tabani (R, G, B): {floor[0]:.4f} {floor[1]:.4f} {floor[2]:.4f}")
    Hn = W // 2
    out = np.zeros((Hn, W, 3), np.float32)
    for c in range(3):
        out[..., c] = np.asarray(Image.fromarray(med[..., c].astype(np.float32), mode="F").resize((W, Hn), Image.BOX))
    R, G, B = out[..., 0], out[..., 1], out[..., 2]
    X = 0.4124 * R + 0.3576 * G + 0.1805 * B
    Y = 0.2126 * R + 0.7152 * G + 0.0722 * B
    Z = 0.0193 * R + 0.1192 * G + 0.9505 * B
    S = np.maximum(X + Y + Z, 1e-12)
    x, y = X / S, Y / S
    n = (x - 0.3320) / np.minimum(0.1858 - y, -1e-6)
    cct = np.clip(449.0 * n ** 3 + 3525.0 * n ** 2 + 6823.3 * n + 5520.33, 2000.0, 15000.0)
    cct = np.where(Y > 1e-6, cct, 4800.0)   # (the dark: an old stars' colour, its light nothing)
    # the scale: the brightest few degrees at mu V mag / arcsec^2
    blur = ndimage.gaussian_filter(Y, sigma=W / 360.0 * 1.5, mode="wrap")
    peak = float(np.percentile(blur, 99.9))
    target = 1361.0 * 10.0 ** (-0.4 * (mu + 26.74)) * 206264.806 ** 2
    k = target / peak
    out *= k
    print(f"  olcek: en parlak birkac derece {target:.3e} W/m^2/sr ({mu} kadir/yay saniyesi^2); ortalama {float(np.mean(Y)) * k:.3e}")
    rgba = np.concatenate([out, (cct / 1e4)[..., None]], axis=2).astype(np.float16)
    dst = os.path.join(OUT, "samanyolu_gok.bin")
    with open(dst, "wb") as f:
        f.write(b"OLSK")
        f.write(np.array([W, Hn], dtype=np.uint32).tobytes())
        f.write(np.ascontiguousarray(rgba).tobytes())
    prev = lin_to_srgb(np.clip(out / (target * 1.2), 0.0, 1.0))
    Image.fromarray(prev).save(os.path.join(OUT, "samanyolu_gok_onizleme.png"))
    print(f"  yazildi assets/olcek/samanyolu_gok.bin: {W}x{Hn}, {os.path.getsize(dst) / 1e6:.0f} MB (onizleme: samanyolu_gok_onizleme.png)")


# ---------------------------------------------------------------------------------------------------------------------
# Galaxies: the 2MASS Redshift Survey (Huchra et al. 2012, ApJS 199, 26; VizieR J/ApJS/199/26): 44,600 galaxies to
# Ks = 11.75 over the whole sky but the Milky Way's plane. Distance from the redshift (H0 = 70; the nearest, whose own
# motions swamp the expansion, no nearer than 2.5 Mpc: the Local Group's members are the scene's own), light from the
# K band (L_V ~ 0.35 L_K, about right for the old stars that dominate it). Same format as the stars ("OLST").
def galaksi(H0=70.0):
    path = fetch("https://vizier.cds.unistra.fr/viz-bin/asu-tsv?-source=J/ApJS/199/26/table3&-out=RAJ2000,DEJ2000,cz,Ktmag&-out.max=unlimited",
                 "2mrs.tsv")
    mpc = 3.0856775814913673e22
    pos, att = [], []
    for line in open(path, encoding="utf-8"):
        if not line[:1].isdigit():
            continue
        f = line.rstrip("\n").split("\t")
        try:
            ra, dec, cz, kt = float(f[0]), float(f[1]), float(f[2]), float(f[3])
        except (ValueError, IndexError):
            continue
        if cz < 150.0:
            continue
        d = max(cz / H0, 2.5)                          # Mpc
        mk = kt - 5.0 * math.log10(d * 1e6 / 10.0)
        lk = 10.0 ** (-0.4 * (mk - 3.28))
        a, b = math.radians(ra), math.radians(dec)
        u = (math.cos(b) * math.cos(a), math.cos(b) * math.sin(a), math.sin(b))
        pos.append([u[0] * d * mpc, u[1] * d * mpc, u[2] * d * mpc])
        # colour: early types redder; without the type, a mean galaxy's (~5500 K)
        att.append([0.35 * lk, 1.08, 0.98, 0.86])
    pos = np.array(pos, np.float64)
    att = np.array(att, np.float32)
    order = np.argsort(-att[:, 0])
    pos, att = pos[order], att[order]
    with open(os.path.join(OUT, "galaksiler.bin"), "wb") as f:
        f.write(b"OLST")
        f.write(np.array([len(pos)], np.uint32).tobytes())
        f.write(np.ascontiguousarray(pos).tobytes())
        f.write(np.ascontiguousarray(att).tobytes())
    print(f"  yazildi assets/olcek/galaksiler.bin: {len(pos)} galaksi (2MRS)")


# ---------------------------------------------------------------------------------------------------------------------
# The cosmic web beyond the survey: a statistical sample, not a map (PLAN §10). A Gaussian random field with a CDM-like
# spectrum (BBKS transfer, Gamma = 0.2, n = 1) moved by the Zel'dovich approximation: particles from a grid displaced by
# the field's gradient gather into walls, filaments and knots around voids, as in the real one. 256^3 cells over a
# 600 Mpc periodic box (2.3 Mpc a cell); stored as the density over its mean, float32 ("OLWB", size, box in Mpc, the
# largest value, then the cells with x fastest).
def ag(N=256, L=600.0, seed=11, growth=1.25):
    rng = np.random.default_rng(seed)
    k1 = np.fft.fftfreq(N, d=L / N) * 2.0 * np.pi            # 1/Mpc
    kz = np.fft.rfftfreq(N, d=L / N) * 2.0 * np.pi
    kx, ky, kzz = np.meshgrid(k1, k1, kz, indexing="ij")
    k2 = kx * kx + ky * ky + kzz * kzz
    k = np.sqrt(k2)
    k[0, 0, 0] = 1.0
    q = k / (0.2 * 0.7)                                       # BBKS with Gamma = Omega_m h = 0.2 (k in h/Mpc)
    T = np.log(1.0 + 2.34 * q) / (2.34 * q) * (1.0 + 3.89 * q + (16.1 * q) ** 2 + (5.46 * q) ** 3 + (6.71 * q) ** 4) ** -0.25
    Pk = k * T * T
    Pk[0, 0, 0] = 0.0
    noise = rng.standard_normal((N, N, N)).astype(np.float32)
    dk = np.fft.rfftn(noise) * np.sqrt(Pk)
    # the linear field's rms at the cell scale sets how far the particles go: about a cell
    delta = np.fft.irfftn(dk, s=(N, N, N)).astype(np.float32)
    dk *= growth / float(delta.std())
    del delta, noise
    psi = []
    for kk in (kx, ky, kzz):
        psi.append(np.fft.irfftn(1j * kk / np.where(k2 > 0, k2, 1.0) * dk, s=(N, N, N)).astype(np.float32))
    del dk, kx, ky, kzz, k2, k, q, T, Pk
    cell = L / N
    # displacements in cells; particles from the grid
    g = np.arange(N, dtype=np.float32)
    X, Y, Z = np.meshgrid(g, g, g, indexing="ij")
    px = (X + psi[0] / cell) % N
    py = (Y + psi[1] / cell) % N
    pz = (Z + psi[2] / cell) % N
    del X, Y, Z, psi
    # cloud in cell
    rho = np.zeros((N, N, N), np.float32)
    ix, iy, iz = np.floor(px).astype(np.int32), np.floor(py).astype(np.int32), np.floor(pz).astype(np.int32)
    fx, fy, fz = px - ix, py - iy, pz - iz
    for dx in (0, 1):
        wx = fx if dx else 1.0 - fx
        for dy in (0, 1):
            wy = fy if dy else 1.0 - fy
            for dz in (0, 1):
                wz = fz if dz else 1.0 - fz
                np.add.at(rho, (((ix + dx) % N).ravel(), ((iy + dy) % N).ravel(), ((iz + dz) % N).ravel()), (wx * wy * wz).ravel())
    rho /= rho.mean()
    # the voids keep the particles' grid (a faint lattice): a Gaussian of 0.8 cells, periodic
    from scipy import ndimage
    rho = ndimage.gaussian_filter(rho, 0.8, mode="wrap")
    rho /= rho.mean()
    print(f"  ag: yogunluk en cok {rho.max():.0f} kat, bos hucre %{100 * (rho < 0.1).mean():.0f}")
    with open(os.path.join(OUT, "ag.bin"), "wb") as f:
        f.write(b"OLWB")
        f.write(np.array([N], np.uint32).tobytes())
        f.write(np.array([L, float(rho.max())], np.float32).tobytes())
        f.write(np.ascontiguousarray(rho.transpose(2, 1, 0).astype(np.float32)).tobytes())   # x fastest, mean 1
    from PIL import Image
    v = np.log1p(rho)
    Image.fromarray(np.clip(v[:, :, N // 2] / v.max() * 255, 0, 255).astype(np.uint8)).resize((768, 768)).save(os.path.join(OUT, "ag_kesit.png"))
    print(f"  yazildi assets/olcek/ag.bin: {N}^3, {L:.0f} Mpc")


# ---------------------------------------------------------------------------------------------------------------------
# The belts (the Minor Planet Center's MPCORB, today's edition): every orbit sampled along its mean anomaly (its time
# average: at the scales where the belts show, an asteroid goes round faster than the eye's shutter), the heliocentric
# distance and the height over the ecliptic binned. The main belt and the Kuiper belt as rings (their surface density
# and thickness by distance), Jupiter's Trojans as their two swarms (count, spread in longitude and distance from
# Jupiter's L4 and L5). Written as JSON (assets/olcek/kusaklar.json).
def asteroit(samples=12):
    import gzip
    import json
    path = fetch("https://minorplanetcenter.net/iau/MPCORB/MPCORB.DAT.gz", "MPCORB.DAT.gz")
    a_l, e_l, i_l, w_l, O_l, M_l, ep_l = [], [], [], [], [], [], []
    started = False

    def unpack_epoch(t):
        # packed date: century letter (I=18, J=19, K=20), two digits of year, month and day as 1-9A-V
        cen = {"I": 1800, "J": 1900, "K": 2000}[t[0]]
        yr = cen + int(t[1:3])
        code = "123456789ABCDEFGHIJKLMNOPQRSTUV"
        mo, dy = code.index(t[3]) + 1, code.index(t[4]) + 1
        import datetime
        return (datetime.date(yr, mo, dy) - datetime.date(2000, 1, 1)).days + 0.5   # days from J2000.0 (noon)

    with gzip.open(path, "rt", encoding="latin-1") as f:
        for line in f:
            if not started:
                if line.startswith("-----"):
                    started = True
                continue
            if len(line) < 103:
                continue
            try:
                a = float(line[92:103]); e = float(line[70:79]); inc = float(line[59:68])
                w = float(line[37:46]); O = float(line[48:57]); M = float(line[26:35])
                ep = unpack_epoch(line[20:25])
            except (ValueError, KeyError, IndexError):
                continue
            if not (0.5 < a < 200.0) or not (0.0 <= e < 0.99):
                continue
            a_l.append(a); e_l.append(e); i_l.append(inc); w_l.append(w); O_l.append(O); M_l.append(M); ep_l.append(ep)
    a = np.array(a_l); e = np.array(e_l); inc = np.radians(i_l); w = np.radians(w_l); O = np.radians(O_l)
    M0 = np.radians(M_l); ep = np.array(ep_l)
    print(f"  MPCORB: {len(a)} yorunge")
    # today (the scene's moment), days from J2000
    t_today = 9774.0 + 0.4162   # 2026-10-05 09:59 UTC
    nmot = 0.01720209895 / a ** 1.5   # rad/day
    out = {}
    # ---- the rings: r and z over the orbits (samples along M)
    def rings(sel, r0, r1, dr):
        aa, ee, ii, ww = a[sel], e[sel], inc[sel], w[sel]
        nb = int(round((r1 - r0) / dr))
        cnt = np.zeros(nb); z2 = np.zeros(nb)
        for k in range(samples):
            Mk = 2.0 * np.pi * (k + 0.5) / samples
            E = Mk + ee * np.sin(Mk)
            for _ in range(6):
                E = E - (E - ee * np.sin(E) - Mk) / (1.0 - ee * np.cos(E))
            r = aa * (1.0 - ee * np.cos(E))
            nu = 2.0 * np.arctan2(np.sqrt(1 + ee) * np.sin(E / 2), np.sqrt(1 - ee) * np.cos(E / 2))
            z = r * np.sin(ii) * np.sin(ww + nu)
            b = np.floor((r - r0) / dr).astype(int)
            ok = (b >= 0) & (b < nb)
            np.add.at(cnt, b[ok], 1.0 / samples)
            np.add.at(z2, b[ok], z[ok] ** 2 / samples)
        zrms = np.sqrt(z2 / np.maximum(cnt, 1e-9))
        rc = r0 + dr * (np.arange(nb) + 0.5)
        return {"r0": r0, "dr": dr, "n": [round(float(x), 2) for x in cnt], "zrms": [round(float(x), 4) for x in zrms],
                "sigma": [float(x) for x in cnt / (2 * np.pi * rc * dr)]}
    trojan = (a > 5.05) & (a < 5.35) & (e < 0.25)
    main = (a < 5.0) & ~trojan
    kuiper = (a > 29.0) & (a < 60.0)
    out["main"] = rings(main, 1.0, 5.6, 0.02)
    out["kuiper"] = rings(kuiper, 25.0, 70.0, 0.25)
    # ---- the Trojans: their mean longitude from Jupiter's today (mean elements, J2000: L 34.396 deg, n 0.0830853 deg/day)
    lamJ = np.radians(34.39644 + 0.08308530 * t_today)
    lam = O[trojan] + w[trojan] + M0[trojan] + nmot[trojan] * (t_today - ep[trojan])
    d = (lam - lamJ + np.pi) % (2.0 * np.pi) - np.pi
    L4 = d > 0
    def swarm(m, sign):
        dd = np.degrees(d[m]) - sign * 60.0
        return {"n": int(m.sum()), "lon": float(np.mean(dd) + sign * 60.0), "sigLon": float(np.std(dd)),
                "sigR": float(np.std(a[trojan][m] * (1 - e[trojan][m] ** 2) ** 0.5))}
    out["trojans"] = {"L4": swarm(L4, 1.0), "L5": swarm(~L4, -1.0)}
    out["counts"] = {"main": int(main.sum()), "trojans": int(trojan.sum()), "kuiper": int(kuiper.sum()), "all": int(len(a))}
    with open(os.path.join(OUT, "kusaklar.json"), "w", encoding="utf-8") as f:
        json.dump(out, f)
    # the program's table: "ring <name> r0 dr bins", then per bin "surface density (per AU^2) z_rms (AU)"; "trojan
    # <L4|L5> count lon sigLon" (degrees from Jupiter)
    with open(os.path.join(OUT, "kusaklar.txt"), "w", encoding="utf-8") as f:
        for name in ("main", "kuiper"):
            g = out[name]
            f.write(f"ring {name} {g['r0']} {g['dr']} {len(g['n'])}\n")
            for sg, z in zip(g["sigma"], g["zrms"]):
                f.write(f"{sg:.3f} {z:.4f}\n")
        for k in ("L4", "L5"):
            t = out["trojans"][k]
            f.write(f"trojan {k} {t['n']} {t['lon']:.3f} {t['sigLon']:.3f}\n")
    print(f"  ana kusak {out['counts']['main']}, Truvalilar {out['counts']['trojans']} (L4 {out['trojans']['L4']['n']}, "
          f"L5 {out['trojans']['L5']['n']}), Kuiper {out['counts']['kuiper']}")


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
    if what in ("galaksi", "hepsi"):
        galaksi()
    if what in ("ag", "hepsi"):
        ag()
    if what in ("asteroit", "hepsi"):
        asteroit()
    if what in ("cie", "hepsi"):
        cie()
    if what in ("samanyolu", "hepsi"):
        samanyolu()
    if what == "kusak":
        from PIL import Image
        Image.MAX_IMAGE_PIXELS = None
        zonal(np.asarray(Image.open(fetch(BMNG + "world.200410.3x21600x10800.jpg", "world.200410.3x21600x10800.jpg")).convert("RGB")))
