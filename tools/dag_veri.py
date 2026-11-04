"""Scene 13 "Dag": the real ground around Kackar's summit, fetched and prepared (assets/dag/, not committed; sources in
assets/dag/KAYNAKLAR.md).

  python tools/dag_veri.py dem        Copernicus DEM tiles (GLO-30, GLO-90 where GLO-30 is not public), re-runnable
  python tools/dag_veri.py mosaic     the tiles as one lat/lon grid at 1 arc second: assets/dag/dem_1s.u16
  python tools/dag_veri.py imgfetch   Sentinel-2 cloudless 2016 web map tiles, re-runnable
  python tools/dag_veri.py imgbuild   the ground's colour as clip levels around the summit: assets/dag/a0.rgb8 ...
  python tools/dag_veri.py rocks      Poly Haven's rock scans (CC0), md5 checked: assets/dag/cc0/
  python tools/dag_veri.py dem90      where GLO-30 is not public (Armenia, Azerbaijan and their border cells, Ararat among them):
                                      Mapzen's Terrain Tiles (SRTM, zoom 12, about 29 m) in place of GLO-90 in dem_1s.u16
"""
import concurrent.futures as cf
import json
import math
import os
import sys
import time
import urllib.error
import urllib.request

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'assets', 'dag')
DL = os.path.join(OUT, '_dl')
UA = {'User-Agent': 'pc_graphics-dag-veri (hobby rendering project)'}

# the summit (refined from the data by the renderer), and how far the ground can be seen at all: 3937 m and 5642 m (Elbrus)
# see each other over about 525 km with the air's usual bending
LAT0, LON0 = 40.835555555555555, 41.161388888888887
RADIUS_KM = 530.0
R = 6371.0
LAT_MIN, LAT_MAX, LON_MIN, LON_MAX = 36, 46, 34, 48    # whole degrees: cells [LAT_MIN, LAT_MAX) x [LON_MIN, LON_MAX)


def dist_km(lat, lon):
    a = np.radians(lat)
    b = math.radians(LAT0)
    dl = np.radians(lon - LON0)
    h = np.sin((a - b) / 2) ** 2 + np.cos(a) * math.cos(b) * np.sin(dl / 2) ** 2
    return 2 * R * np.arcsin(np.sqrt(h))


def tile_near(lat, lon):
    la = np.linspace(lat, lat + 1, 9)
    lo = np.linspace(lon, lon + 1, 9)
    g = np.meshgrid(la, lo, indexing='ij')
    return float(dist_km(g[0], g[1]).min())


def dem_name(res, lat, lon):
    return f'Copernicus_DSM_COG_{res}_N{lat:02d}_00_E{lon:03d}_00_DEM'


def fetch(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return 'have'
    tmp = path + '.part'
    for attempt in range(4):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r, open(tmp, 'wb') as f:
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b)
            os.replace(tmp, path)
            return 'got'
        except urllib.error.HTTPError as e:
            if e.code in (403, 404):
                return 'none'
            time.sleep(1 + attempt)
        except Exception:
            time.sleep(1 + attempt)
    return 'fail'


def one_dem(cell):
    lat, lon = cell
    n30 = dem_name(10, lat, lon)
    p30 = os.path.join(DL, 'dem', n30 + '.tif')
    r = fetch(f'https://copernicus-dem-30m.s3.amazonaws.com/{n30}/{n30}.tif', p30)
    if r in ('have', 'got'):
        return cell, 30, r
    n90 = dem_name(30, lat, lon)
    p90 = os.path.join(DL, 'dem', n90 + '.tif')
    r2 = fetch(f'https://copernicus-dem-90m.s3.amazonaws.com/{n90}/{n90}.tif', p90)
    if r2 in ('have', 'got'):
        return cell, 90, r2
    return cell, 0, r2


def cmd_dem():
    cells = [(la, lo) for la in range(LAT_MIN, LAT_MAX) for lo in range(LON_MIN, LON_MAX) if tile_near(la, lo) <= RADIUS_KM]
    print(len(cells), 'cells within', RADIUS_KM, 'km', flush=True)
    done = {}
    t0 = time.time()
    with cf.ThreadPoolExecutor(12) as ex:
        for k, (cell, res, how) in enumerate(ex.map(one_dem, cells)):
            done[f'{cell[0]},{cell[1]}'] = res
            if k % 10 == 0:
                print(k, cell, res, how, f'{time.time() - t0:.0f} s', flush=True)
    vals = list(done.values())
    print('GLO-30', vals.count(30), 'GLO-90', vals.count(90), 'none (sea)', vals.count(0), f'{time.time() - t0:.0f} s', flush=True)
    with open(os.path.join(DL, 'dem_index.json'), 'w') as f:
        json.dump(done, f, indent=0)


def read_tile(item):
    import cv2
    import rasterio
    key, res = item
    if res == 0:
        return None
    lat, lon = [int(x) for x in key.split(',')]
    name = dem_name(10 if res == 30 else 30, lat, lon)
    with rasterio.open(os.path.join(DL, 'dem', name + '.tif')) as d:
        a = d.read(1).astype(np.float32)
    if a.shape != (3600, 3600):
        a = cv2.resize(a, (3600, 3600), interpolation=cv2.INTER_LINEAR)
    v = np.clip(np.round((a + 500.0) * 8.0), 0, 65535).astype(np.uint16)
    return lat, lon, v, res


def cmd_mosaic():
    with open(os.path.join(DL, 'dem_index.json')) as f:
        idx = json.load(f)
    h = (LAT_MAX - LAT_MIN) * 3600
    w = (LON_MAX - LON_MIN) * 3600
    path = os.path.join(OUT, 'dem_1s.u16')
    m = np.memmap(path, dtype=np.uint16, mode='w+', shape=(h, w))
    m[:] = 4000   # sea level: (0 + 500) * 8
    src = np.zeros((LAT_MAX - LAT_MIN, LON_MAX - LON_MIN), np.uint8)
    t0 = time.time()
    with cf.ThreadPoolExecutor(12) as ex:
        for k, r in enumerate(ex.map(read_tile, idx.items())):
            if r is None:
                continue
            lat, lon, v, res = r
            r0 = (LAT_MAX - (lat + 1)) * 3600
            c0 = (lon - LON_MIN) * 3600
            m[r0:r0 + 3600, c0:c0 + 3600] = v
            src[lat - LAT_MIN, lon - LON_MIN] = res
            if k % 20 == 0:
                print(k, lat, lon, res, f'{time.time() - t0:.0f} s', flush=True)
    m.flush()
    with open(os.path.join(OUT, 'dem_1s.json'), 'w') as f:
        json.dump({'width': w, 'height': h, 'lat_top': LAT_MAX, 'lon_left': LON_MIN, 'per_degree': 3600, 'code': 'u16: h = v / 8 - 500',
                   'sources': src.tolist()}, f)
    print('mosaic', w, h, f'{time.time() - t0:.0f} s')


# the ground's colour: Sentinel-2 cloudless 2016 (EOX), CC BY 4.0 for that year's layer (from 2018 on they are NC: not used)
S2_LAYER = 's2cloudless_3857'
# clip levels: (name, metres a cell, cells on a side, web map zoom it is read from)
LEVELS = [('a0', 8.0, 8192, 14), ('a1', 16.0, 16384, 13), ('a2', 64.0, 16384, 11)]


def merc_tile(lat, lon, z):
    n = 2.0 ** z
    x = (lon + 180.0) / 360.0 * n
    y = (1.0 - np.log(np.tan(np.radians(lat)) + 1.0 / np.cos(np.radians(lat))) / math.pi) / 2.0 * n
    return x, y


def ae_inverse(mx, mz):
    # the azimuthal equidistant plane around the summit (x east, z south, metres) to lat, lon (degrees), on a sphere
    s = np.hypot(mx, mz) / (R * 1000.0)
    az = np.arctan2(mx, -mz)
    la0 = math.radians(LAT0)
    sin_lat = np.sin(la0) * np.cos(s) + math.cos(la0) * np.sin(s) * np.cos(az)
    lat = np.arcsin(np.clip(sin_lat, -1, 1))
    lon = math.radians(LON0) + np.arctan2(np.sin(az) * np.sin(s) * math.cos(la0), np.cos(s) - math.sin(la0) * sin_lat)
    return np.degrees(lat), np.degrees(lon)


def one_img(job):
    z, y, x = job
    d = os.path.join(DL, 's2', str(z))
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, f'{y}_{x}.jpg')
    return fetch(f'https://tiles.maps.eox.at/wmts/1.0.0/{S2_LAYER}/default/g/{z}/{y}/{x}.jpg', p)


def level_tiles(cell, n, z):
    half = cell * n / 2.0
    t = np.linspace(-half, half, 512)
    gx, gz = np.meshgrid(t, t)
    lat, lon = ae_inverse(gx, gz)
    x, y = merc_tile(lat, lon, z)
    tiles = set()
    for r in range(gx.shape[0]):
        xa, xb = int(np.floor(x[r].min())) - 1, int(np.floor(x[r].max())) + 1
        ya, yb = int(np.floor(y[r].min())) - 1, int(np.floor(y[r].max())) + 1
        for yy in range(ya, yb + 1):
            for xx in range(xa, xb + 1):
                tiles.add((yy, xx))
    return tiles


def land_tiles(z):
    # web map tiles with land in them (the sea's own colour is never read: it is drawn)
    import cv2
    with open(os.path.join(OUT, 'dem_1s.json')) as f:
        meta = json.load(f)
    m = np.memmap(os.path.join(OUT, 'dem_1s.u16'), dtype=np.uint16, mode='r', shape=(meta['height'], meta['width']))
    st = 36
    small = np.asarray(m[::st, ::st]) != 4000
    small = cv2.dilate(small.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    rows, cols = np.nonzero(small)
    lat = meta['lat_top'] - rows * st / 3600.0
    lon = meta['lon_left'] + cols * st / 3600.0
    x, y = merc_tile(lat, lon, z)
    return set(zip(np.floor(y).astype(int).tolist(), np.floor(x).astype(int).tolist()))


def cmd_imgfetch():
    t0 = time.time()
    for name, cell, n, z in LEVELS:
        tiles = level_tiles(cell, n, z)
        land = land_tiles(z)
        wide = set()
        for (y, x) in land:
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    wide.add((y + dy, x + dx))
        jobs = [(z, y, x) for (y, x) in sorted(tiles) if (y, x) in wide]
        print(name, 'zoom', z, len(tiles), 'tiles,', len(jobs), 'with land', flush=True)
        got = {'have': 0, 'got': 0, 'none': 0, 'fail': 0}
        with cf.ThreadPoolExecutor(10) as ex:
            for k, r in enumerate(ex.map(one_img, jobs)):
                got[r] += 1
                if k % 1000 == 0:
                    print(' ', k, got, f'{time.time() - t0:.0f} s', flush=True)
        print(' ', name, got, f'{time.time() - t0:.0f} s', flush=True)


def build_level(name, cell, n, z):
    # the grid's colour, block by block: the plane's cell to lat, lon to the web map's pixel, bilinear in sRGB code values
    import cv2
    half = cell * n / 2.0
    out = np.memmap(os.path.join(OUT, name + '.rgb8'), dtype=np.uint8, mode='w+', shape=(n, n, 3))
    tile_dir = os.path.join(DL, 's2', str(z))
    tile_cache = {}
    blank = np.zeros((256, 256, 3), np.uint8)

    def tile(y, x):
        k = (y, x)
        t = tile_cache.get(k)
        if t is None:
            p = os.path.join(tile_dir, f'{y}_{x}.jpg')
            a = cv2.imread(p, cv2.IMREAD_COLOR) if os.path.exists(p) else None
            t = blank if a is None or a.shape[:2] != (256, 256) else a[:, :, ::-1]
            tile_cache[k] = t
        return t

    B = 1024
    t0 = time.time()
    for r0 in range(0, n, B):
        for c0 in range(0, n, B):
            rs = np.arange(r0, min(r0 + B, n))
            cs = np.arange(c0, min(c0 + B, n))
            gx, gz = np.meshgrid((cs + 0.5) * cell - half, (rs + 0.5) * cell - half)
            lat, lon = ae_inverse(gx, gz)
            x, y = merc_tile(lat, lon, z)
            px = x * 256.0 - 0.5
            py = y * 256.0 - 0.5
            x0, x1 = int(np.floor(px.min())) - 1, int(np.ceil(px.max())) + 2
            y0, y1 = int(np.floor(py.min())) - 1, int(np.ceil(py.max())) + 2
            ta, tb = x0 // 256, (x1 - 1) // 256
            tc, td = y0 // 256, (y1 - 1) // 256
            canvas = np.zeros(((td - tc + 1) * 256, (tb - ta + 1) * 256, 3), np.uint8)
            for ty in range(tc, td + 1):
                for tx in range(ta, tb + 1):
                    canvas[(ty - tc) * 256:(ty - tc + 1) * 256, (tx - ta) * 256:(tx - ta + 1) * 256] = tile(ty, tx)
            m = cv2.remap(canvas, (px - ta * 256).astype(np.float32), (py - tc * 256).astype(np.float32), cv2.INTER_LINEAR)
            out[r0:r0 + len(rs), c0:c0 + len(cs)] = m
        if len(tile_cache) > 40000:
            tile_cache.clear()
        print(name, r0, '/', n, f'{time.time() - t0:.0f} s', flush=True)
    out.flush()


def cmd_imgbuild():
    only = sys.argv[2] if len(sys.argv) > 2 else None
    for name, cell, n, z in LEVELS:
        if only and name != only:
            continue
        build_level(name, cell, n, z)
    with open(os.path.join(OUT, 'renk.json'), 'w') as f:
        json.dump({'levels': [{'name': a, 'cell': b, 'n': c, 'zoom': d} for a, b, c, d in LEVELS], 'layer': S2_LAYER,
                   'lat0': LAT0, 'lon0': LON0, 'R': R * 1000.0}, f)


def one_terrarium(job):
    z, y, x = job
    d = os.path.join(DL, 'terrarium', str(z))
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, f'{y}_{x}.png')
    return fetch(f'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png', p)


def cmd_dem90():
    # the cells GLO-30 does not cover: SRTM by way of the Terrain Tiles. Its water is not always flat, so GLO-90's flat cells
    # (the lakes, flattened in that model) stay as they were
    import cv2
    with open(os.path.join(OUT, 'dem_1s.json')) as f:
        meta = json.load(f)
    src = meta['sources']
    cells = [(LAT_MIN + i, LON_MIN + j) for i, r in enumerate(src) for j, v in enumerate(r) if v == 90]
    print(len(cells), 'cells', cells, flush=True)
    z = 12
    t0 = time.time()
    jobs = set()
    for lat, lon in cells:
        xa, ya = merc_tile(np.array([lat + 1.0]), np.array([lon]), z)
        xb, yb = merc_tile(np.array([lat]), np.array([lon + 1.0]), z)
        for y in range(int(ya[0]) - 1, int(yb[0]) + 2):
            for x in range(int(xa[0]) - 1, int(xb[0]) + 2):
                jobs.add((z, y, x))
    jobs = sorted(jobs)
    print(len(jobs), 'tiles', flush=True)
    got = {'have': 0, 'got': 0, 'none': 0, 'fail': 0}
    with cf.ThreadPoolExecutor(12) as ex:
        for k, r in enumerate(ex.map(one_terrarium, jobs)):
            got[r] += 1
            if k % 500 == 0:
                print(' ', k, got, f'{time.time() - t0:.0f} s', flush=True)
    print(got, f'{time.time() - t0:.0f} s', flush=True)
    if got['fail'] or got['none']:
        raise SystemExit('tiles missing: the cells stay as they are')
    m = np.memmap(os.path.join(OUT, 'dem_1s.u16'), dtype=np.uint16, mode='r+', shape=(meta['height'], meta['width']))
    tdir = os.path.join(DL, 'terrarium', str(z))
    for lat, lon in cells:
        r0 = (LAT_MAX - (lat + 1)) * 3600
        c0 = (lon - LON_MIN) * 3600
        old = np.array(m[r0:r0 + 3600, c0:c0 + 3600]).astype(np.float32) / 8.0 - 500.0
        rows = (lat + 1) - np.arange(3600) / 3600.0
        cols = lon + np.arange(3600) / 3600.0
        gr, gc = np.meshgrid(rows, cols, indexing='ij')
        x, y = merc_tile(gr, gc, z)
        px = x * 256.0 - 0.5
        py = y * 256.0 - 0.5
        ta, tb = int(np.floor(px.min() / 256)) - 0, int(np.floor(px.max() / 256)) + 1
        tc, td = int(np.floor(py.min() / 256)) - 0, int(np.floor(py.max() / 256)) + 1
        canvas = np.zeros(((td - tc + 1) * 256, (tb - ta + 1) * 256), np.float32)
        for ty in range(tc, td + 1):
            for tx in range(ta, tb + 1):
                a = cv2.imread(os.path.join(tdir, f'{ty}_{tx}.png'), cv2.IMREAD_COLOR)
                if a is None:
                    raise SystemExit(f'no tile {ty} {tx}')
                a = a.astype(np.float32)
                # Terrarium: (red * 256 + green + blue / 256) - 32768, OpenCV reads BGR
                canvas[(ty - tc) * 256:(ty - tc + 1) * 256, (tx - ta) * 256:(tx - ta + 1) * 256] = a[:, :, 2] * 256.0 + a[:, :, 1] + a[:, :, 0] / 256.0 - 32768.0
        new = cv2.remap(canvas, (px - ta * 256).astype(np.float32), (py - tc * 256).astype(np.float32), cv2.INTER_LINEAR)
        # GLO-90's flat water stays, and its open sea
        flat = cv2.erode((cv2.absdiff(old, cv2.blur(old, (5, 5))) < 0.02).astype(np.uint8), np.ones((3, 3), np.uint8)) > 0
        merged = np.where(flat, old, new)
        d = new - old
        print(f'cell {lat} {lon}: SRTM - GLO-90 mean {d.mean():.2f} m, std {d.std():.2f} m, max {new.max():.0f} m, flat share {flat.mean():.3f}', flush=True)
        m[r0:r0 + 3600, c0:c0 + 3600] = np.clip(np.round((merged + 500.0) * 8.0), 0, 65535).astype(np.uint16)
        src[lat - LAT_MIN][lon - LON_MIN] = 31
    m.flush()
    meta['sources'] = src
    with open(os.path.join(OUT, 'dem_1s.json'), 'w') as f:
        json.dump(meta, f)
    print('done', f'{time.time() - t0:.0f} s', flush=True)


def ph_get(url, path, md5):
    import hashlib
    if os.path.exists(path):
        with open(path, 'rb') as f:
            if hashlib.md5(f.read()).hexdigest() == md5:
                return 'have'
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r:
        b = r.read()
    if hashlib.md5(b).hexdigest() != md5:
        raise RuntimeError('md5: ' + url)
    with open(path, 'wb') as f:
        f.write(b)
    return 'got'


def cmd_rocks():
    d = os.path.join(OUT, 'cc0')
    os.makedirs(d, exist_ok=True)

    def files(pid):
        with urllib.request.urlopen(urllib.request.Request(f'https://api.polyhaven.com/files/{pid}', headers=UA), timeout=60) as r:
            return json.load(r)

    # the two textures
    for pid, name in (('aerial_rocks_04', 'kayaA'), ('gray_rocks', 'kayaB')):
        f = files(pid)
        for m, fn in (('Diffuse', 'albedo'), ('nor_gl', 'normal'), ('Displacement', 'disp')):
            e = f[m]['8k']['jpg']
            print(pid, m, ph_get(e['url'], os.path.join(d, f'{name}_{fn}.jpg'), e['md5']), flush=True)
    # the models: geometry from the glTF's own bin, colour and normals at this resolution
    for pid, res in (('boulder_01', '8k'), ('rock_07', '4k'), ('rock_09', '4k')):
        f = files(pid)
        md = os.path.join(d, pid)
        os.makedirs(md, exist_ok=True)
        g = f['gltf'][res]['gltf']
        print(pid, 'gltf', ph_get(g['url'], os.path.join(md, f'{pid}.gltf'), g['md5']), flush=True)
        for rel, e in g['include'].items():
            if rel.endswith('.bin'):
                print(pid, rel, ph_get(e['url'], os.path.join(md, os.path.basename(rel)), e['md5']), flush=True)
        for m, fn in (('Diffuse', 'albedo'), ('nor_gl', 'normal')):
            e = f[m][res]['jpg']
            print(pid, m, ph_get(e['url'], os.path.join(md, f'{fn}.jpg'), e['md5']), flush=True)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'dem':
        cmd_dem()
    elif what == 'mosaic':
        cmd_mosaic()
    elif what == 'imgfetch':
        cmd_imgfetch()
    elif what == 'imgbuild':
        cmd_imgbuild()
    elif what == 'rocks':
        cmd_rocks()
    elif what == 'dem90':
        cmd_dem90()
    else:
        print(__doc__)
