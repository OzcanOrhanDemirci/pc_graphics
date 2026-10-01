"""Fener (scene 2) value study: composition thumbnails before any 3D work.

A 2D painter with a real camera: every shape is placed in metres (lighthouse 30 m, island at D metres, ridges km away)
and projected, so the numbers of the chosen thumbnail carry straight into the 3D scene. Light is approximated:
- fog: exponential height fog + thin uniform haze, optical depth integrated analytically per pixel;
- moonlit in-scatter painted from a two-lobe phase function around the moon direction;
- the lighthouse beam: single scattering ray-marched per pixel with equi-angular sampling around the lamp;
- water: perspective plane, wave normals, Fresnel, reflection sampled from the rendered layers, Beckmann glints.
It is a study (no shadows inside the fog, billboarded shapes), not the renderer.

usage: python tools/fener_etud.py [--variant A|B|C|all] [--phi DEG] [--out DIR] [--w 1600]
"""
import argparse, math, os
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

# ---------------------------------------------------------------- helpers

def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)

def hash1(i, seed=0):
    x = np.sin(np.asarray(i, np.float64) * 12.9898 + seed * 78.233) * 43758.5453
    return x - np.floor(x)

def vnoise1(x, seed=0):
    i = np.floor(x); f = x - i; u = f * f * (3 - 2 * f)
    return (1 - u) * hash1(i, seed) + u * hash1(i + 1, seed)

def fbm1(x, seed=0, oct=5, lac=2.03, gain=0.5):
    s, a, n = 0.0, 0.5, 0.0
    for k in range(oct):
        s = s + a * vnoise1(x, seed + k * 7.1); n += a; x = x * lac; a *= gain
    return s / n

def ridged1(x, seed=0, oct=6):
    s, a, n = 0.0, 0.5, 0.0
    for k in range(oct):
        v = 1.0 - np.abs(2.0 * vnoise1(x, seed + k * 3.7) - 1.0)
        s = s + a * v * v; n += a; x = x * 2.1; a *= 0.5
    return s / n

def vnoise2(x, y, seed=0):
    ix, iy = np.floor(x), np.floor(y); fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    def h(a, b):
        return hash1(a * 157.0 + b * 113.0, seed)
    return ((1 - ux) * (1 - uy) * h(ix, iy) + ux * (1 - uy) * h(ix + 1, iy)
            + (1 - ux) * uy * h(ix, iy + 1) + ux * uy * h(ix + 1, iy + 1))

def fbm2(x, y, seed=0, oct=5):
    s, a, n = 0.0, 0.5, 0.0
    for k in range(oct):
        s = s + a * vnoise2(x, y, seed + k * 5.3); n += a; x = x * 2.02; y = y * 2.02; a *= 0.5
    return s / n

def hg(cos, g):
    return (1 - g * g) / (4 * math.pi * np.power(1 + g * g - 2 * g * cos, 1.5))

def norm(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)

# ---------------------------------------------------------------- variants (metres, degrees)

VARIANTS = {
    # A "Pencere": framed between two near trunks, lighthouse centre, horizon a little under the middle.
    'A': dict(name='Pencere', hc=5.0, pitch=5.0, fov=38.0, D=470.0, Ht=44.0,
              trunks=[(-2.2, 5.0, 0.20)], spruces=[(-9.5, 11.0, 30.0), (12.0, 13.0, 28.0), (-14.0, 19.0, 26.0)],
              boughs='none', ground_h=0.12, moon=(-0.15, 0.30)),
    # B "Yucelik": low horizon, island far and small, fjord walls looming; one trunk on the left, right side open.
    'B': dict(name='Yucelik', hc=4.0, pitch=10.0, fov=44.0, D=900.0, Ht=46.0,
              trunks=[(-2.6, 5.5, 0.16)], spruces=[(-9.6, 9.5, 30.0), (-14.5, 16.0, 26.0)],
              boughs='none', ground_h=0.09, moon=(-0.16, 0.36)),
    # C "Yakin": island close, lighthouse big, cabin and path readable; low boughs across the top, trunk on the right.
    'C': dict(name='Yakin', hc=7.0, pitch=4.0, fov=34.0, D=330.0, Ht=40.0,
              trunks=[(-2.4, 5.2, 0.17)], spruces=[(-9.0, 12.0, 26.0), (9.5, 10.5, 30.0)],
              boughs='none', ground_h=0.13, moon=(-0.17, 0.27)),
}

# ---------------------------------------------------------------- the study

class Study:
    def __init__(self, V, W=1600, phi=38.0, ss=3, seed=1):
        self.V, self.W, self.H, self.ss, self.seed = V, W, W * 9 // 16, ss, seed
        W, H = self.W, self.H
        self.f = (H / 2) / math.tan(math.radians(V['fov']) / 2)
        p = math.radians(V['pitch'])
        self.C = np.array([0.0, V['hc'], 0.0])
        self.fw = np.array([0.0, math.sin(p), math.cos(p)])
        self.up = np.array([0.0, math.cos(p), -math.sin(p)])
        self.rt = np.array([1.0, 0.0, 0.0])
        ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
        xn = (xs + 0.5 - W / 2) / self.f; yn = (H / 2 - ys - 0.5) / self.f
        self.dir = norm(xn[..., None] * self.rt + yn[..., None] * self.up + self.fw)
        self.horizon = H / 2 + self.f * math.tan(p)
        # light and atmosphere (artistic units: radiance values before exposure)
        self.phi = math.radians(phi)                     # beam azimuth: 0 = toward the camera, + = to the camera's left
        m = V['moon']; md = np.array([m[0], m[1], 1.0]); self.moon = md / np.linalg.norm(md)
        self.moonE = 1.0                                 # moonlight irradiance scale
        self.rho0, self.Hf, self.rhou = 1 / 520.0, 55.0, 1 / 22000.0   # height fog at sea level, scale height, haze
        self.fogtint = np.array([0.60, 0.72, 0.86])
        self.fogK, self.beamK, self.beamCap = 0.0055, 2.6, 0.09
        self.lamp = np.array([0.0, V['Ht'] + 31.5, V['D']])
        self.beamI, self.beamW, self.leak = 9.0e5, math.radians(3.2), 9.0e2
        self.beamWv, self.beamTilt = math.radians(3.5), math.radians(-1.5)
        self.spillK, self.spillWv, self.spillTilt = 0.02, math.radians(12.0), math.radians(-15.0)
        self.window = np.array([74.0, 0.0, V['D'] - 22.0])
        self.depth = np.full((H, W), np.inf)
        self.layers = []

    # --- projection and drawing
    def proj(self, P):
        P = np.atleast_2d(np.asarray(P, np.float64)); v = P - self.C
        xc, yc, zc = v @ self.rt, v @ self.up, v @ self.fw
        return np.stack([self.W / 2 + self.f * xc / zc, self.H / 2 - self.f * yc / zc], -1)

    def new_mask(self):
        return Image.new('L', (self.W * self.ss, self.H * self.ss), 0)

    def poly(self, draw, pts_world, fill=255):
        q = self.proj(pts_world) * self.ss
        draw.polygon([tuple(p) for p in q], fill=fill)

    def finish(self, img):
        a = img.resize((self.W, self.H), Image.BOX)
        return np.asarray(a, np.float64) / 255.0

    # --- atmosphere
    def tau(self, y0, dy, t):
        """optical depth from height y0 along a ray with vertical direction dy for distance t"""
        k = 1.0 / self.Hf
        small = np.abs(dy) < 1e-4
        dyz = np.where(small, 1e-4, dy)
        a = np.exp(-y0 * k); b = np.exp(-(y0 + dyz * t) * k)
        h = self.rho0 * np.where(small, a * t, (a - b) / (dyz * k))
        return h + self.rhou * np.minimum(t, 30000.0)

    def fogcol(self, d):
        c = np.clip(d @ self.moon, -1, 1)
        ph = 0.80 * hg(c, 0.25) + 0.20 * hg(c, 0.92)
        up = np.clip(d[..., 1], -1, 1)
        L = self.moonE * (0.55 + 4.0 * ph) * (0.30 + 0.70 * np.exp(-np.maximum(up, 0) * 4.0))
        return L[..., None] * self.fogtint * self.fogK

    def sky(self, d):
        up = np.maximum(d[..., 1], 1e-3)
        t_inf = self.tau(self.C[1], up, 1e6)
        T = np.exp(-t_inf)[..., None]
        night = np.array([0.010, 0.014, 0.022]) * 0.10
        # thin cloud veil: horizontal streaks in a sky-dome parametrisation, brighter near the moon
        u = d[..., 0] / up; v = 1.0 / up
        cl = fbm2(u * 0.9, v * 3.2, seed=self.seed + 11, oct=4)
        cl = smoothstep(0.45, 0.85, cl) * smoothstep(0.02, 0.15, up) * 0.8
        c = np.clip(d @ self.moon, -1, 1)
        moonlit = (0.25 + 3.0 * hg(c, 0.75))[..., None]
        sky = night * (1 - cl[..., None] * 0.6) + cl[..., None] * moonlit * np.array([0.0030, 0.0036, 0.0045])
        # moon disc (enlarged for art: 0.55 deg radius) veiled by cloud
        ang = np.arccos(c); r = math.radians(0.55)
        disc = (1 - smoothstep(r * 0.80, r * 1.10, ang)) * (1 - 0.75 * cl) * 0.8
        sky = sky + disc[..., None] * np.array([0.30, 0.31, 0.32])
        return sky * T + self.fogcol(d) * (1 - T)

    def fog_mix(self, col, t, d):
        tt = self.tau(self.C[1], d[..., 1], t)
        T = np.exp(-tt)[..., None]
        return col * T + self.fogcol(d) * (1 - T)

    def beam_profile(self, w, leak=True):
        """lamp intensity toward unit directions w (..., 3): two opposite beams + leak.
        Each beam: narrow in azimuth; in elevation a near-horizontal main lobe (it passes high over the camera)
        plus a weak, wide, downward spill that grazes the forest and the sea as it sweeps."""
        az = np.arctan2(-w[..., 0], -w[..., 2]); el = np.arcsin(np.clip(w[..., 1], -1, 1))
        vert = np.exp(-((el - self.beamTilt) / self.beamWv) ** 2) + self.spillK * np.exp(-((el - self.spillTilt) / self.spillWv) ** 2)
        g = 0.0
        for off in (0.0, math.pi):
            da = (az - self.phi - off + math.pi) % (2 * math.pi) - math.pi
            g = g + np.exp(-(da / self.beamW) ** 2)
        return self.beamI * g * vert + (self.leak if leak else 0.0)

    def face(self):
        """how squarely the beam's azimuth faces the camera (0..1): drives the glare flash, not the fog"""
        v = self.C - self.lamp
        az = math.atan2(-v[0], -v[2])
        da = (az - self.phi + math.pi) % (2 * math.pi) - math.pi
        return math.exp(-(da / (self.beamW * 1.3)) ** 2)

    def beam_scatter(self, tmax, nseg=72, chunk=40000):
        """single scattering of the lamp along every pixel ray up to tmax (equi-angular, stratified)"""
        H, W = self.H, self.W
        d = self.dir.reshape(-1, 3); tm = np.minimum(tmax.reshape(-1), 6000.0)
        out = np.zeros(d.shape[0]); core = np.zeros(d.shape[0])
        pyx = np.mgrid[0:H, 0:W].reshape(2, -1).astype(np.float64)
        ign = (52.9829189 * ((0.06711056 * pyx[1] + 0.00583715 * pyx[0]) % 1.0)) % 1.0
        L = self.lamp; C = self.C
        rng = np.random.default_rng(self.seed)
        for s in range(0, d.shape[0], chunk):
            dd = d[s:s + chunk]; tt = tm[s:s + chunk]
            delta = (L - C) @ dd.T
            perp = np.linalg.norm((L - C)[None] - delta[:, None] * dd, axis=1) + 1e-3
            ta = np.arctan2(-delta, perp); tb = np.arctan2(tt - delta, perp)
            u = (np.arange(nseg)[None] + ign[s:s + chunk, None]) / nseg
            th = ta[:, None] + u * (tb - ta)[:, None]
            t = delta[:, None] + perp[:, None] * np.tan(th)
            pdf = perp[:, None] / ((tb - ta)[:, None] * (perp[:, None] ** 2 + (t - delta[:, None]) ** 2))
            P = C[None, None] + t[..., None] * dd[:, None]
            v = P - L; r = np.linalg.norm(v, axis=-1) + 1e-3; w = v / r[..., None]
            I = self.beam_profile(w); Ib = I - self.leak
            cos = np.einsum('nsk,nk->ns', w, -dd)
            ph = 0.85 * hg(cos, 0.15) + 0.15 * hg(cos, 0.70)   # art: front/side ratio ~5 (a real fog: ~100)
            sig = self.rho0 * np.exp(-P[..., 1] / self.Hf) + self.rhou
            Tc = np.exp(-self.tau(C[1], dd[:, 1:2], t))
            Tl = np.exp(-self.tau(L[1], w[..., 1], r))
            wgt = sig * Tc * Tl / r ** 2 * ph / pdf / nseg
            out[s:s + chunk] = np.sum(wgt * I, axis=1); core[s:s + chunk] = np.sum(wgt * Ib * ph * 25.0, axis=1)
        return out.reshape(H, W), core.reshape(H, W)

    def lamp_irr(self, P):
        v = P - self.lamp; r = np.linalg.norm(v, axis=-1) + 1e-3
        I = self.beam_profile(v / r[..., None])
        return I / r ** 2 * np.exp(-self.tau(self.lamp[1], v[..., 1] / r, r))

    # --- shapes
    def conifer(self, draw, x, y0, z, h, rnd, lean=0.0, wf=0.23):
        n = int(7 + rnd.integers(0, 4)); w = h * wf * rnd.uniform(0.85, 1.15)
        top = (x + lean * h, y0 + h)
        R, Lp = [], []
        prev_r, prev_l = 0.0, 0.0
        for k in range(1, n + 1):
            fy = (k / n) ** 0.95
            yt = y0 + h - fy * h * 0.86
            wk = w * fy * rnd.uniform(0.80, 1.18); wl = w * fy * rnd.uniform(0.80, 1.18)
            cx = x + lean * h * (1 - fy * 0.86)
            dr = h * 0.035 * rnd.uniform(0.5, 1.4); dl = h * 0.035 * rnd.uniform(0.5, 1.4)
            R += [(cx + prev_r * 0.45, yt + h * 0.06), (cx + wk, yt - dr)]
            Lp += [(cx - prev_l * 0.45, yt + h * 0.06), (cx - wl, yt - dl)]
            prev_r, prev_l = wk, wl
        tw = h * 0.025
        pts = [top] + R + [(x + tw, y0 + h * 0.10), (x + tw, y0 - 2), (x - tw, y0 - 2), (x - tw, y0 + h * 0.10)] + Lp[::-1]
        self.poly(draw, [(px, py, z) for px, py in pts])

    def ground(self, x):
        V = self.V; Ht = V['Ht']
        hill = Ht * np.exp(-np.abs(x / 95.0) ** 1.7) + 6.0 * smoothstep(-235, -205, x) * smoothstep(-120, -170, x)
        shelf = 12.5 * smoothstep(175, 120, x) * smoothstep(35, 60, x)
        shore = smoothstep(-275, -215, x) * smoothstep(235, 180, x)
        g = np.maximum(hill, shelf) * shore + 2.5 * fbm1(x / 9.0, 3) * shore
        return g

    # ------------------------------------------------------------ build all layers
    def build(self):
        V, rnd = self.V, np.random.default_rng(self.seed)
        D = V['D']
        L = []   # (mask, depth plane z, albedo rgb, kind)

        # giant fjord walls and ridges: three planes, km away
        for z, hmin, hmax, sd, span in ((16000, 1500, 3400, 21, 34000), (10000, 800, 2000, 22, 24000), (6000, 250, 900, 23, 16000)):
            m = self.new_mask(); dr = ImageDraw.Draw(m)
            xs = np.linspace(-span, span, 1400)
            y = hmin + (hmax - hmin) * ridged1(xs / (span * 0.11) + sd, sd)
            if z == 16000:   # colossal fjord walls rising out of the mist, one left of the lighthouse, one right
                y = y + 1600 * np.exp(-((xs + 3200) / 2200) ** 2) + 900 * np.exp(-((xs - 4600) / 1800) ** 2)
            pts = [(x_, y_, z) for x_, y_ in zip(xs, y)] + [(span, -60, z), (-span, -60, z)]
            self.poly(dr, pts); L.append((self.finish(m), z, np.array([0.010, 0.012, 0.015]), 'land'))

        # back islands with tiny forests
        for z, x0, x1, hh, sd in ((2500, 260, 1300, 75, 31), (1700, -1250, -380, 60, 32)):
            m = self.new_mask(); dr = ImageDraw.Draw(m)
            xs = np.linspace(x0, x1, 300)
            u = (xs - x0) / (x1 - x0)
            y = hh * np.sin(np.pi * u) ** 0.7 * (0.75 + 0.5 * fbm1(xs / 60.0, sd))
            self.poly(dr, [(a, b, z) for a, b in zip(xs, y)] + [(x1, -3, z), (x0, -3, z)])
            for x in np.arange(x0 + 25, x1 - 25, 9.0):
                if rnd.random() < 0.8:
                    uu = (x - x0) / (x1 - x0)
                    yy = hh * np.sin(np.pi * uu) ** 0.7 * (0.75 + 0.5 * fbm1(np.array([x / 60.0]), sd)[0])
                    self.conifer(dr, x, yy - 3, z, rnd.uniform(14, 26), rnd)
            L.append((self.finish(m), z, np.array([0.012, 0.014, 0.017]), 'land'))

        # the island: back forest, terrain + lighthouse, path + cabin, front forest
        xs = np.linspace(-300, 260, 700)
        gy = self.ground(xs)
        # back forest: dense and tall, it makes the island's silhouette
        m = self.new_mask(); dr = ImageDraw.Draw(m)
        for zb, step, hmin, hmax in ((D + 90, 3.6, 20, 34), (D + 40, 4.2, 18, 30)):
            for x in np.arange(-250, 215, step):
                g = self.ground(np.array([x]))[0]
                if g > 1.0 and rnd.random() < 0.9:
                    hh = rnd.uniform(hmin, hmax) * (0.75 + 0.25 * smoothstep(0, 20, g))
                    if abs(x) < 30:
                        hh = min(hh, 12.0 + abs(x) * 0.45)
                    self.conifer(dr, x + rnd.uniform(-1.5, 1.5), g - 2, zb, hh, rnd, lean=rnd.uniform(-0.02, 0.02))
        L.append((self.finish(m), D + 60, np.array([0.016, 0.018, 0.021]), 'forest'))
        # terrain
        m = self.new_mask(); dr = ImageDraw.Draw(m)
        self.poly(dr, [(a, b, D) for a, b in zip(xs, gy)] + [(260, -2, D), (-300, -2, D)])
        # rocks at the waterline
        for x in np.arange(-285, 250, 7.0):
            if rnd.random() < 0.6 and self.ground(np.array([x]))[0] < 9:
                w = rnd.uniform(3, 9); h = rnd.uniform(1.5, 5)
                self.poly(dr, [(x - w, -0.5, D - 30), (x - w * 0.6, h, D - 30), (x + w * 0.3, h * 1.1, D - 30), (x + w, -0.5, D - 30)])
        L.append((self.finish(m), D, np.array([0.020, 0.022, 0.026]), 'land'))
        # lighthouse: tower bands, gallery, lantern (emissive, separate), dome
        base = V['Ht'] - 0.5; th = 36.0; rb, rt = 4.4, 3.1
        def rad(y):
            return rb + (rt - rb) * (y - base) / th
        bands = []
        nb = 6
        for k in range(nb):
            y0 = base + th * k / nb; y1 = base + th * (k + 1) / nb
            mm = self.new_mask(); d2 = ImageDraw.Draw(mm)
            self.poly(d2, [(-rad(y0), y0, D), (rad(y0), y0, D), (rad(y1), y1, D), (-rad(y1), y1, D)])
            bands.append((self.finish(mm), D, np.array([0.085, 0.088, 0.092]) if k % 2 == 0 else np.array([0.034, 0.034, 0.036]), 'tower'))
        mm = self.new_mask(); d2 = ImageDraw.Draw(mm)
        gy0 = base + th
        self.poly(d2, [(-3.9, gy0 - 0.3, D), (3.9, gy0 - 0.3, D), (3.9, gy0 + 0.35, D), (-3.9, gy0 + 0.35, D)])   # gallery deck
        for xr in np.linspace(-3.8, 3.8, 9):   # railing posts
            self.poly(d2, [(xr - 0.05, gy0, D), (xr + 0.05, gy0, D), (xr + 0.05, gy0 + 1.1, D), (xr - 0.05, gy0 + 1.1, D)])
        self.poly(d2, [(-3.9, gy0 + 1.0, D), (3.9, gy0 + 1.0, D), (3.9, gy0 + 1.15, D), (-3.9, gy0 + 1.15, D)])
        ly0, ly1 = gy0 + 0.35, gy0 + 4.2
        self.poly(d2, [(-2.5, ly1, D), (2.5, ly1, D), (1.6, ly1 + 1.6, D), (0.0, ly1 + 2.3, D), (-1.6, ly1 + 1.6, D)])   # dome
        self.poly(d2, [(-0.06, ly1 + 2.2, D), (0.06, ly1 + 2.2, D), (0.03, ly1 + 3.6, D), (-0.03, ly1 + 3.6, D)])       # vane
        for xm in np.linspace(-2.3, 2.3, 5):   # lantern mullions
            self.poly(d2, [(xm - 0.07, ly0, D), (xm + 0.07, ly0, D), (xm + 0.07, ly1, D), (xm - 0.07, ly1, D)])
        self.poly(d2, [(-2.5, ly0, D), (2.5, ly0, D), (2.5, ly0 + 0.6, D), (-2.5, ly0 + 0.6, D)])   # lantern base wall
        gal = (self.finish(mm), D, np.array([0.012, 0.012, 0.013]), 'dark')
        mm = self.new_mask(); d2 = ImageDraw.Draw(mm)
        self.poly(d2, [(-2.4, ly0 + 0.6, D), (2.4, ly0 + 0.6, D), (2.4, ly1, D), (-2.4, ly1, D)])
        lantern = self.finish(mm)
        self.lantern_mask, self.lantern_center = lantern, self.proj([0, (ly0 + ly1) / 2, D])[0]
        self.lamp = np.array([0.0, (ly0 + ly1) / 2 + 0.3, D])
        # path and cabin
        zc = D - 22.0
        mm = self.new_mask(); d2 = ImageDraw.Draw(mm)
        tpath = np.linspace(0, 1, 60)
        px_ = 5 + 62 * tpath + 6 * np.sin(tpath * 5.0); pz_ = D - 6 - 15 * tpath
        py_ = np.array([self.ground(np.array([x]))[0] for x in px_]) + 0.05
        wpath = 0.9
        left = [(x - 0.0, y + wpath * 0.0, z) for x, y, z in zip(px_, py_, pz_)]
        q = self.proj(left) * self.ss
        d2.line([tuple(p) for p in q], fill=255, width=max(2, int(self.ss * self.f * 1.6 / D)))
        path = (self.finish(mm), D, np.array([0.030, 0.032, 0.034]), 'path')
        cx0, gyc = 74.0, float(self.ground(np.array([74.0]))[0])
        mm = self.new_mask(); d2 = ImageDraw.Draw(mm)
        lean = 0.35
        self.poly(d2, [(cx0 - 4.0, gyc - 0.5, zc), (cx0 + 4.0, gyc - 0.5, zc), (cx0 + 4.0 + lean, gyc + 3.8, zc), (cx0 - 4.0 + lean, gyc + 3.8, zc)])
        self.poly(d2, [(cx0 - 5.0 + lean, gyc + 3.6, zc), (cx0 + 5.1 + lean, gyc + 3.5, zc), (cx0 + 0.25 + lean, gyc + 7.0, zc), (cx0 - 0.2 + lean, gyc + 6.85, zc)])
        self.poly(d2, [(cx0 + 2.2, gyc + 5.0, zc), (cx0 + 2.9, gyc + 4.6, zc), (cx0 + 2.9, gyc + 7.4, zc), (cx0 + 2.2, gyc + 7.4, zc)])
        cabin = (self.finish(mm), zc, np.array([0.030, 0.028, 0.026]), 'cabin')
        mm = self.new_mask(); d2 = ImageDraw.Draw(mm)
        wx, wy = cx0 - 1.6 + lean * 0.4, gyc + 1.4
        self.poly(d2, [(wx - 0.7, wy, zc), (wx + 0.7, wy, zc), (wx + 0.7, wy + 1.1, zc), (wx - 0.7, wy + 1.1, zc)])
        win = self.finish(mm)
        mm = self.new_mask(); d2 = ImageDraw.Draw(mm)
        self.poly(d2, [(wx - 0.04, wy, zc), (wx + 0.04, wy, zc), (wx + 0.04, wy + 0.95, zc), (wx - 0.04, wy + 0.95, zc)])
        self.poly(d2, [(wx - 0.55, wy + 0.45, zc), (wx + 0.55, wy + 0.45, zc), (wx + 0.55, wy + 0.52, zc), (wx - 0.55, wy + 0.52, zc)])
        win = np.clip(win - self.finish(mm), 0, 1)
        self.window = np.array([wx, wy + 0.5, zc])
        # front forest: rows climbing the hill face (front rows low and near), clear around the cabin and the lantern
        m = self.new_mask(); dr = ImageDraw.Draw(m)
        for r_, zr in enumerate((D - 70, D - 52, D - 34, D - 16)):
            lift = (r_ + 1) / 4.0
            gx_ = np.linspace(-280, 245, 300)
            gb = np.maximum(self.ground(gx_) * lift - 1.5, -1.0) * smoothstep(-280, -250, gx_) * smoothstep(245, 215, gx_)
            self.poly(dr, [(a, b, zr) for a, b in zip(gx_, gb)] + [(245, -2, zr), (-280, -2, zr)])   # the slope these trees stand on
            for x in np.arange(-265, 230, 3.4 + r_ * 0.4):
                g = self.ground(np.array([x]))[0]
                if g < 1.2 or rnd.random() > 0.8 or (50 < x < 98 and r_ < 3) or (54 < x < 94):
                    continue
                base_y = g * lift - 3.0
                hgt = rnd.uniform(14, 26) * (0.85 + 0.3 * rnd.random())
                if abs(x) < 28:
                    hgt = min(hgt, max(4.0, V['Ht'] + 6.0 + abs(x) * 0.4 - base_y))
                self.conifer(dr, x + rnd.uniform(-1.5, 1.5), base_y, zr, hgt, rnd, lean=rnd.uniform(-0.025, 0.025))
        front = (self.finish(m), D - 40, np.array([0.018, 0.020, 0.023]), 'forest')
        L += bands + [gal, path, (win * 0, zc, np.zeros(3), 'none'), cabin, front]
        self.win_mask, self.win_z = win, zc

        # foreground: near shore, trunks with dead twigs, a few drooping boughs (one layer, near-black)
        m = self.new_mask(); dr = ImageDraw.Draw(m)
        zg = 14.0; gh = V['hc'] - 1.7
        xs = np.linspace(-30, 30, 600)
        # rocky lip of the near island: low-frequency boulders, a few notches
        yv = gh - 2.9 + 1.1 * fbm1(xs / 3.5, 41, oct=3) + 0.4 * smoothstep(0.55, 0.9, fbm1(xs / 1.3, 43, oct=2))
        self.poly(dr, [(a, b, zg) for a, b in zip(xs, yv)] + [(30, -30, zg), (-30, -30, zg)])
        q = self.proj(np.stack([xs, yv, np.full_like(xs, zg)], -1))
        for i in range(0, len(xs), 3):   # sparse grass tufts: 3-5 thin blades each
            if rnd.random() < 0.22:
                bx, by = q[i]
                for _ in range(rnd.integers(3, 6)):
                    hgt = rnd.uniform(0.008, 0.026) * self.H; ang = rnd.uniform(-0.45, 0.45); wb = rnd.uniform(0.6, 1.2)
                    ox = bx + rnd.uniform(-3, 3)
                    dr.polygon([((ox - wb) * self.ss, (by + 2) * self.ss), ((ox + wb) * self.ss, (by + 2) * self.ss),
                                ((ox + math.sin(ang) * hgt) * self.ss, (by - hgt * math.cos(ang)) * self.ss)], fill=255)
        for (tx, tz, tr) in V['trunks']:
            wob = lambda y: 0.04 * math.sin(y * 0.7 + tx)
            ys_ = np.linspace(gh - 3, gh + 60, 40)
            right = [(tx + tr * (1 - 0.004 * (y - gh)) + wob(y), y, tz) for y in ys_]
            left = [(tx - tr * (1 - 0.004 * (y - gh)) + wob(y), y, tz) for y in ys_[::-1]]
            self.poly(dr, right + left)
            # dead lower twigs: thin, sharp, slightly drooping
            for k in range(int(rnd.integers(6, 11))):
                yb = gh + rnd.uniform(0.8, 9.0); sd = rnd.choice([-1, 1]); ln = rnd.uniform(0.25, 1.1)
                th = rnd.uniform(0.012, 0.028); dro = rnd.uniform(-0.15, 0.35)
                x0 = tx + sd * tr * 0.9
                self.poly(dr, [(x0, yb + th, tz), (x0, yb - th, tz), (x0 + sd * ln, yb - dro * ln, tz)])
            # a few living boughs high up, drooping into the top of the frame
            nb = 2 if V['boughs'] == 'both' else 0
            for k in range(nb):
                yb = V['hc'] + 1.6 + k * 0.9 + rnd.uniform(-0.2, 0.2)
                sd = 1 if tx < 0 else -1
                self.bough(dr, tx + sd * tr * 0.8, yb, tz, sd, rnd.uniform(1.4, 2.6), rnd)
        for (sx, sz, sh) in V.get('spruces', []):   # near spruces: whorls of drooping boughs, jagged needle fringe
            self.near_spruce(dr, sx, gh - 2.0, sz, sh, rnd)
        if V['boughs'] == 'top':
            for k in range(3):
                self.bough(dr, 4.1, V['hc'] + 2.1 + k * 0.45, 6.5, -1, rnd.uniform(3.0, 4.8), rnd)
        self.fg = self.finish(m)
        L.append((self.fg, 7.0, np.array([0.004, 0.0045, 0.005]), 'fg'))
        self.layers = L

    def near_spruce(self, dr, x, y0, z, h, rnd):
        """a spruce seen from a few metres: solid drooping tiers with pointed tips and a needle fringe underneath"""
        tw = 0.22
        self.poly(dr, [(x - tw, y0 - 2, z), (x + tw, y0 - 2, z), (x + tw * 0.4, y0 + h, z), (x - tw * 0.4, y0 + h, z)])
        y = y0 + h - 0.4
        k = 0
        while y > y0 + 0.8:
            u = (y - y0) / h
            sp = 0.55 + 0.55 * (1 - u)                      # tier spacing grows toward the ground
            for sd in (-1, 1):
                L = (0.27 * h * (1 - u) ** 0.9 + 0.35) * rnd.uniform(0.8, 1.12)
                for fork in range(2):                 # each whorl: a main branch and a shorter, offset one
                    Lf = L * (1.0 if fork == 0 else rnd.uniform(0.45, 0.75))
                    yf = y - (0 if fork == 0 else sp * rnd.uniform(0.25, 0.5))
                    dro = rnd.uniform(0.26, 0.52); n = max(10, int(Lf * 10)); t = np.linspace(0, 1, n)
                    bx = x + sd * (tw * 0.5 + Lf * t)
                    wig = 0.06 * Lf * (fbm1(t * 6 + rnd.uniform(0, 50), 7, oct=3) - 0.5)
                    upper = yf - dro * Lf * t + 0.10 * Lf * t ** 3 + wig
                    thick = sp * (1.35 if fork == 0 else 0.9) * (1 - t) ** 1.05 * (0.8 + 0.4 * fbm1(t * 9 + rnd.uniform(0, 50), 8, oct=2))
                    lower = upper - thick - 0.03
                    self.poly(dr, [(a, b, z) for a, b in zip(bx, upper)] + [(a, b, z) for a, b in zip(bx[::-1], lower[::-1])])
                    # needles: swept toward the tip, longer underneath, short on top
                    for i in range(1, n):
                        for _ in range(3):
                            ll = rnd.uniform(0.06, 0.22) * (0.7 + 0.5 * t[i])
                            a_ = sd * rnd.uniform(0.35, 1.15)          # angle from straight down, toward the tip
                            ox = bx[i] + rnd.uniform(-0.05, 0.05); oy = lower[i] + 0.03
                            self.poly(dr, [(ox - 0.018, oy + 0.02, z), (ox + 0.018, oy + 0.02, z), (ox + math.sin(a_) * ll, oy - math.cos(a_) * ll, z)])
                        if rnd.random() < 0.6:
                            ll = rnd.uniform(0.04, 0.12)
                            a_ = sd * rnd.uniform(0.6, 1.3)
                            ox = bx[i]; oy = upper[i] - 0.03
                            self.poly(dr, [(ox - 0.015, oy - 0.02, z), (ox + 0.015, oy - 0.02, z), (ox + math.sin(a_) * ll, oy + math.cos(a_) * ll, z)])
                    tipx, tipy = bx[-1], upper[-1]
                    self.poly(dr, [(tipx, tipy + 0.03, z), (tipx, tipy - 0.05, z), (tipx + sd * rnd.uniform(0.15, 0.4), tipy - rnd.uniform(0.0, 0.15), z)])
            y -= sp * rnd.uniform(0.85, 1.15); k += 1

    def bough(self, dr, x0, y0, z, side, ln, rnd, droop=0.42, fringe=1.0):
        """spruce bough seen from below: a thin drooping stem with a dense fringe of short hanging needles"""
        n = max(12, int(ln * 9)); t = np.linspace(0, 1, n)
        bx = x0 + side * ln * t; by = y0 - droop * ln * t ** 1.8
        thick = 0.035 * (1 - t) + 0.008
        top = [(a, b + c, z) for a, b, c in zip(bx, by, thick)]; bot = [(a, b - c, z) for a, b, c in zip(bx, by, thick)]
        self.poly(dr, top + bot[::-1])
        for i in range(2, n):
            for _ in range(2):
                ll = rnd.uniform(0.06, 0.24) * fringe * (1.15 - 0.6 * t[i]); a = rnd.uniform(-0.15, 0.45) * side
                ox, oy = bx[i], by[i] - thick[i]
                tip = (ox + math.sin(a) * ll, oy - math.cos(a) * ll)
                self.poly(dr, [(ox - 0.012, oy + 0.01, z), (ox + 0.012, oy + 0.01, z), (tip[0], tip[1], z)])
            if rnd.random() < 0.35:   # side shoot
                ll = rnd.uniform(0.12, 0.30) * fringe * (1.1 - 0.5 * t[i])
                a = side * rnd.uniform(0.6, 1.2)
                ox, oy = bx[i], by[i]
                self.poly(dr, [(ox, oy + 0.008, z), (ox, oy - 0.008, z), (ox + math.sin(a) * ll, oy - math.cos(a) * ll * 0.6, z)])

    # ------------------------------------------------------------ render
    def render(self):
        H, W = self.H, self.W; d = self.dir
        ambient = 0.020   # moonlight on silhouettes (art: deliberately weak)
        sky = self.sky(d)
        img = sky.copy()
        depth = np.full((H, W), 1e7)
        hit_any = np.zeros((H, W))
        # 1) layers without water: also kept as the reflection source
        def composite(base, with_beam_light=True):
            out = base.copy(); dep = depth.copy()
            for (a, z, alb, kind) in self.layers:
                if kind == 'none':
                    continue
                t = z / np.maximum(d[..., 2], 1e-4)
                P = self.C + t[..., None] * d
                col = alb * ambient * (1.0 + 2.0 * np.clip(-d @ self.moon, 0, 1))[..., None]
                if with_beam_light and kind in ('forest', 'land', 'tower', 'cabin', 'path'):
                    E = self.lamp_irr(P)
                    E = E / (1.0 + E / 0.6)          # art: crowns catch the beam, they do not burn
                    # crowns and ridges facing the lamp catch it: weight the upper edge of the silhouette
                    edge = np.clip(a - np.roll(a, 3, axis=0), 0, 1)
                    edge = ndimage.gaussian_filter(edge, 1.2)
                    k = 0.35 + 2.5 * edge if kind == 'forest' else np.full_like(a, 0.3)
                    if kind == 'tower':
                        k = np.full_like(a, 0.10)
                    col = col + alb[None, None] * E[..., None] * k[..., None] * 0.9
                col = self.fog_mix(col, t, d)
                out = out * (1 - a[..., None]) + col * a[..., None]
                dep = np.where(a > 0.5, t, dep)
            return out, dep
        above, dep = composite(sky)
        self.depth = dep
        # 2) water under the islands
        below = d[..., 1] < -1e-4
        tw = np.where(below, self.C[1] / np.maximum(-d[..., 1], 1e-4), 1e7)
        S = self.C + tw[..., None] * d
        # wave normals: sharp, choppy crests, octaves faded by pixel footprint
        foot = tw / self.f / np.maximum(-d[..., 1], 0.02)
        gx = np.zeros((H, W)); gz = np.zeros((H, W))
        sig_tot = 0.13
        wr = np.random.default_rng(90)
        nw = 40
        for k in range(nw):
            wl = math.exp(wr.uniform(math.log(0.35), math.log(14.0)))
            ang = math.pi / 2 + wr.normal(0, 0.55)               # travelling toward the shore, spread +-30 deg
            kx, kz = math.cos(ang), math.sin(ang)
            kk = 2 * math.pi / wl; A = 0.050 * wl / (2 * math.pi) * wr.uniform(0.6, 1.2)   # steepness ~0.05
            ph = kk * (S[..., 0] * kx + S[..., 2] * kz) + wr.uniform(0, 2 * math.pi)
            fade = np.exp(-(foot / (wl * 0.22)) ** 2)
            sl = -A * kk * (np.sin(ph) + 0.7 * np.sin(2 * ph)) * fade   # Stokes 2nd harmonic: sharp crests
            gx += sl * kx; gz += sl * kz
        res2 = gx ** 2 + gz ** 2
        sig_u = np.sqrt(np.maximum(sig_tot ** 2 - np.minimum(res2, sig_tot ** 2) * 0.8, 0.004))
        n = norm(np.stack([-gx, np.ones_like(gx), -gz], -1))
        v = -d
        cosv = np.clip(np.sum(n * v, -1), 1e-3, 1)
        F = 0.02 + 0.98 * (1 - cosv) ** 5
        r = d - 2 * np.sum(d * n, -1)[..., None] * n
        r[..., 1] = np.abs(r[..., 1])
        # reflection: the ray from S along r meets the island plane z = D; sample the composited layers there
        D = self.V['D']
        tr = np.where(r[..., 2] > 1e-3, (D - S[..., 2]) / np.maximum(r[..., 2], 1e-3), 1e6)
        Q = S + np.clip(tr, 0, 1e6)[..., None] * r
        Q = np.where((tr > 0)[..., None] & (tr < 1e6)[..., None], Q, self.C + r * 1e5)
        qs = self.proj(Q.reshape(-1, 3)).reshape(H, W, 2)
        refl = np.stack([ndimage.map_coordinates(above[..., c], [qs[..., 1] - 0.5, qs[..., 0] - 0.5], order=1, mode='nearest') for c in range(3)], -1)
        offscreen = (qs[..., 1] < 0)[..., None]
        refl = np.where(offscreen, self.sky(norm(r)), refl)
        refl = ndimage.gaussian_filter(refl, (2.0, 0.6, 0))   # choppy water smears reflections vertically
        deep = np.array([0.0015, 0.0028, 0.0034])
        water = refl * F[..., None] + deep * ambient * 8 * (1 - F[..., None])
        # Beckmann glints for the moon and the lamp
        def glint(ldir, Lrad, sig):
            hvec = norm(ldir + v)
            ch = np.clip(np.sum(hvec * n, -1), 1e-3, 1)   # relative to the resolved wave normal
            tan2 = (1 - ch ** 2) / ch ** 2
            Dm = np.exp(-tan2 / sig ** 2) / (math.pi * sig ** 2 * ch ** 4)
            return Lrad * Dm * F / (4 * cosv)
        moon_g = glint(np.broadcast_to(self.moon, d.shape), 0.9, sig_u)
        spark = 1.0
        water = water + (moon_g * spark)[..., None] * np.array([0.80, 0.88, 1.0]) * 0.0010
        vl = self.lamp - S; rl = np.linalg.norm(vl, axis=-1)
        lamp_g = glint(vl / rl[..., None], 1.0, sig_u) * self.lamp_irr(S) * 0.05
        water = water + lamp_g[..., None] * np.array([1.0, 0.98, 0.94])
        vw = self.window - S; rw = np.linalg.norm(vw, axis=-1)
        win_g = glint(vw / rw[..., None], 1.0, sig_u) * 1.2 / rw ** 2 * np.exp(-self.tau(self.C[1], d[..., 1], tw))
        water = self.fog_mix(water, tw, d)
        warm = np.zeros((H, W, 3))
        warm += (win_g * 6.0)[..., None] * np.array([1.0, 0.42, 0.10])
        base = np.where(below[..., None], water, sky)
        img, dep = composite(base)
        warm *= (dep > 1e6)[..., None] | (dep > tw)[..., None]
        self.depth = np.minimum(dep, np.where(below, tw, 1e7))
        # 3) beam in-scatter up to the first surface
        bs, core = self.beam_scatter(np.minimum(self.depth, 6000.0))
        bs = ndimage.gaussian_filter(bs, 0.8)
        B = bs * self.beamK
        B = B / (1.0 + B / self.beamCap)          # art: soft ceiling on in-scattered beam light (Inside clamps its fog too)
        beam = B[..., None] * np.array([0.92, 0.96, 1.0])
        # drizzle visible only inside the beam
        rnd = np.random.default_rng(self.seed + 5)
        rain = np.zeros((H, W))
        for _ in range(int(W * H / 2500)):
            x0, y0 = rnd.integers(0, W), rnd.integers(0, H); ln = rnd.integers(6, 18)
            for s in range(ln):
                xx, yy = int(x0 + s * 0.22), y0 + s
                if 0 <= xx < W and 0 <= yy < H:
                    rain[yy, xx] = 1.0
        rain = ndimage.gaussian_filter(rain, 0.5) * (self.depth > 30)
        core = ndimage.gaussian_filter(core, 1.0)
        rain_vis = smoothstep(0.2, 1.0, core / (np.percentile(core, 99.7) + 1e-9))
        img = img + beam + (rain * rain_vis)[..., None] * beam * 0.6
        # 4) emissives: lantern glass, cabin window
        cam_dir = self.C - self.lamp; cam_r = np.linalg.norm(cam_dir)
        I_cam = self.beam_profile((cam_dir / cam_r)[None])[0]
        lantern_L = 6.0 + 60.0 * self.face()
        T_l = math.exp(-float(self.tau(self.C[1], np.array([(self.lamp[1] - self.C[1]) / cam_r]), np.array([cam_r]))[0]))
        emis = self.lantern_mask[..., None] * lantern_L * T_l * np.array([1.0, 0.98, 0.95]) * 0.05
        T_w = float(np.exp(-self.tau(self.C[1], np.array([0.0]), np.array([np.linalg.norm(self.window - self.C)]))[0]))
        flick = 1.0
        warm += self.win_mask[..., None] * np.array([1.0, 0.45, 0.12]) * 0.16 * T_w * flick
        img = img + emis
        # a second, tiny light far away on the horizon: the world goes on beyond the fog
        far = np.array([2700.0, 30.0, 11000.0]); fp = self.proj(far)[0]
        Tf = float(np.exp(-self.tau(self.C[1], np.array([(far[1] - self.C[1]) / np.linalg.norm(far - self.C)]), np.array([np.linalg.norm(far - self.C)]))[0]))
        ys_, xs_ = np.mgrid[0:H, 0:W]
        dot = np.exp(-((xs_ - fp[0]) ** 2 + (ys_ - fp[1]) ** 2) / (2 * 0.9 ** 2))
        img = img + dot[..., None] * np.array([0.95, 0.97, 1.0]) * 0.05 * max(Tf, 0.15)
        self.I_cam, self.lantern_L = I_cam, lantern_L
        return img, warm

    def post(self, img, warm):
        H, W = self.H, self.W
        hdr = img + warm
        # wide "buttery" glow = atmospheric scattering of everything; narrow glow for the bright bits only
        lum = hdr @ np.array([0.2126, 0.7152, 0.0722])
        wide = np.stack([ndimage.gaussian_filter(hdr[..., c], W * 0.05) for c in range(3)], -1)
        bright = hdr * smoothstep(0.08, 0.6, lum)[..., None]
        nar = sum(np.stack([ndimage.gaussian_filter(bright[..., c], s) for c in range(3)], -1) * wgt
                  for s, wgt in ((2.0, 0.30), (7.0, 0.20), (22.0, 0.12), (70.0, 0.08)))
        out = hdr * 0.95 + wide * 0.05 + nar * 0.45
        # glare star on the lamp, grows as the beam turns toward the camera (monochrome, like Inside's flashlight flares)
        face_i = self.face()
        ys, xs = np.mgrid[0:H, 0:W]
        cx, cy = self.lantern_center
        r2 = ((xs - cx) ** 2 + (ys - cy) ** 2) / W ** 2
        flare = face_i * (1.2 * np.exp(-r2 / (2 * 0.007 ** 2)) + 0.07 * np.exp(-r2 / (2 * 0.045 ** 2)) + 0.010 * np.exp(-r2 / (2 * 0.18 ** 2)))
        out = out + (flare * (1 - 0.85 * self.fg))[..., None] * np.array([0.97, 0.99, 1.0])
        # god rays through the foreground when the beam faces the camera (screen-space, radial toward the lamp)
        face = self.face()
        if face > 0.01:
            cx, cy = self.lantern_center
            src = np.clip(lum - 0.05, 0, None) * (1 - self.fg)
            acc = np.zeros((H, W)); ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
            N = 48
            for i in range(N):
                s = i / N
                sx = xs + (cx - xs) * s * 0.9; sy = ys + (cy - ys) * s * 0.9
                acc += ndimage.map_coordinates(src, [sy, sx], order=1, mode='nearest') * (1 - s) ** 1.5
            acc /= N
            acc *= 1.0 - 0.9 * self.fg          # shafts live in the air beyond the near trees, not on them
            out = out + acc[..., None] * np.array([0.9, 0.95, 1.0]) * 0.9 * face
        # vignette
        ys, xs = np.mgrid[0:H, 0:W]
        rr = ((xs - W / 2) / (W / 2)) ** 2 + ((ys - H / 2) / (H / 2)) ** 2
        out = out * (1 - 0.28 * smoothstep(0.3, 2.0, rr))[..., None]
        # exposure + filmic curve (ACES fit), slight cool lift in the blacks, sRGB, TPDF dither
        x = out * 10.5
        a, b, c, dd, e = 2.51, 0.03, 2.43, 0.59, 0.14
        y = np.clip((x * (a * x + b)) / (x * (c * x + dd) + e), 0, 1)
        y = y + np.array([0.004, 0.006, 0.009]) * (1 - y)
        srgb = np.where(y <= 0.0031308, 12.92 * y, 1.055 * np.power(np.maximum(y, 1e-9), 1 / 2.4) - 0.055)
        rng = np.random.default_rng(3)
        tp = (rng.random(srgb.shape) + rng.random(srgb.shape) - 1.0)
        q = np.clip(np.floor(srgb * 255 + 0.5 + tp), 0, 255).astype(np.uint8)
        return q

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--variant', default='all'); ap.add_argument('--phi', type=float, default=38.0)
    ap.add_argument('--out', default='captures/fener'); ap.add_argument('--w', type=int, default=1600)
    ap.add_argument('--tag', default='')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    keys = list(VARIANTS) if a.variant == 'all' else [a.variant]
    for k in keys:
        st = Study(VARIANTS[k], W=a.w, phi=a.phi)
        st.build()
        img, warm = st.render()
        q = st.post(img, warm)
        fn = os.path.join(a.out, f'etud_{k}_{VARIANTS[k]["name"]}{a.tag}.png')
        Image.fromarray(q).save(fn)
        print(fn, 'lamp->camera intensity ratio', round(st.I_cam / st.beamI, 4), 'face', round(st.face(), 3))

if __name__ == '__main__':
    main()
