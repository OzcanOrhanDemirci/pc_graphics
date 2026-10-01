"""Where does the prism's spectrum land? 2D trace in the horizontal plane (prism axis vertical), same Sellmeier
and same prism geometry as scene.cpp. Prints, for each yaw, the wall hit points of 400 / 550 / 700 nm and the
deviation; the vertical drop of the sunbeam (elevation) is applied along the travelled distance.

  python tools/prism_plan.py --x 7.2 --z 1.2 --y 1.1 [--az 20 --el 10 --side 0.16]
"""
import argparse
import math

L, W = 6 * 1.82, 4 * 1.82


def sellmeier(lam_nm, B, C):
    l2 = (lam_nm * 1e-3) ** 2
    return math.sqrt(1 + sum(b * l2 / (l2 - c) for b, c in zip(B, C)))


def n_sf11(l):
    return sellmeier(l, (1.73759695, 0.313747346, 1.89878101), (0.013188707, 0.0623068142, 155.23629))


def refract(d, n, eta):  # eta = n_t / n_i ; n faces the incoming side
    ci = -(d[0] * n[0] + d[1] * n[1])
    k = 1 - (1 - ci * ci) / (eta * eta)
    if k < 0:
        return None
    a = 1 / eta
    return (d[0] * a + n[0] * (a * ci - math.sqrt(k)), d[1] * a + n[1] * (a * ci - math.sqrt(k)))


def seg_hit(o, d, a, b):
    ex, ez = b[0] - a[0], b[1] - a[1]
    den = d[0] * (-ez) - d[1] * (-ex)
    if abs(den) < 1e-12:
        return None
    rx, rz = a[0] - o[0], a[1] - o[1]
    t = (rx * (-ez) - rz * (-ex)) / den
    s = (d[0] * rz - d[1] * rx) / den
    if t > 1e-6 and 0 <= s <= 1:
        return t
    return None


def trace(px, pz, side, yaw, d, lam):
    r = side / math.sqrt(3)
    v = [(px + r * math.cos(math.radians(yaw + 90 + 120 * k)), pz + r * math.sin(math.radians(yaw + 90 + 120 * k))) for k in range(3)]
    o = (px - d[0] * 2, pz - d[1] * 2)
    inside = False
    n_in = n_sf11(lam)
    total = 0.0
    for _ in range(6):
        best = None
        for k in range(3):
            a, b = v[k], v[(k + 1) % 3]
            t = seg_hit(o, d, a, b)
            if t is not None and (best is None or t < best[0]):
                best = (t, a, b)
        if best is None:
            return o, d, inside, total
        t, a, b = best
        o = (o[0] + d[0] * t, o[1] + d[1] * t)
        total += t
        ex, ez = b[0] - a[0], b[1] - a[1]
        nx, nz = ez, -ex
        ln = math.hypot(nx, nz)
        nx, nz = nx / ln, nz / ln
        cx, cz = (v[0][0] + v[1][0] + v[2][0]) / 3, (v[0][1] + v[1][1] + v[2][1]) / 3
        if (o[0] - cx) * nx + (o[1] - cz) * nz < 0:
            nx, nz = -nx, -nz          # outward normal
        if d[0] * nx + d[1] * nz < 0:  # entering
            nd = refract(d, (nx, nz), n_in)
            inside = True
        else:
            nd = refract(d, (-nx, -nz), 1 / n_in)
            if nd is None:  # total internal reflection
                dn = d[0] * nx + d[1] * nz
                nd = (d[0] - 2 * dn * nx, d[1] - 2 * dn * nz)
            else:
                inside = False
        d = nd
        o = (o[0] + d[0] * 1e-5, o[1] + d[1] * 1e-5)
    return o, d, inside, total


def wall_hit(o, d):
    best = None
    for name, a, b in (("sol duvar z=0", (0, 0), (L, 0)), ("sag duvar z=W", (0, W), (L, W)),
                       ("yakin duvar x=0", (0, 0), (0, W)), ("uzak duvar x=L", (L, 0), (L, W))):
        t = seg_hit(o, d, a, b)
        if t is not None and (best is None or t < best[0]):
            best = (t, name, (o[0] + d[0] * t, o[1] + d[1] * t))
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--x", type=float, required=True)
    ap.add_argument("--z", type=float, required=True)
    ap.add_argument("--y", type=float, default=1.1, help="height of the beam at the prism")
    ap.add_argument("--az", type=float, default=20)
    ap.add_argument("--el", type=float, default=10)
    ap.add_argument("--side", type=float, default=0.16)
    ap.add_argument("--yaw0", type=float, default=0)
    ap.add_argument("--yaw1", type=float, default=120)
    a = ap.parse_args()
    th = math.radians(a.az)
    d0 = (-math.sin(th), math.cos(th))
    slope = math.tan(math.radians(a.el))
    for yaw in [y * 0.5 for y in range(int(a.yaw0 * 2), int(a.yaw1 * 2) + 1)]:
        row = []
        for lam in (400, 550, 700):
            o, d, inside, _ = trace(a.x, a.z, a.side, yaw, d0, lam)
            if inside:
                row = None
                break
            h = wall_hit(o, d)
            if h is None:
                row = None
                break
            t, name, p = h
            dev = math.degrees(math.acos(max(-1, min(1, d[0] * d0[0] + d[1] * d0[1]))))
            row.append((lam, name, p, a.y - slope * (t + math.hypot(o[0] - a.x, o[1] - a.z)), dev, t))
        if not row:
            continue
        spread = math.hypot(row[0][2][0] - row[2][2][0], row[0][2][1] - row[2][2][1])
        desc = "  ".join(f"{lam}nm:{name} ({p[0]:.2f},{p[1]:.2f}) y={y:.2f} dev={dev:.1f}" for lam, name, p, y, dev, t in row)
        print(f"yaw {yaw:5.1f}: spread {spread:.2f} m  {desc}")


if __name__ == "__main__":
    main()
