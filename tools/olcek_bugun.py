"""Sahne 7 "Olcek": "bugun"un gercek konumlari (PLAN §3).

"Bugun" = 5 Ekim 2026, sahildeki noktada (Inciralti, Izmir) Gunes'in tam tepe noktasindan gectigi an (gercek gunes
oglesi). Bu arac o ani bulur ve sahnenin kullandigi her konumu, hizi ve yonu astropy + JPL DE440s ile hesaplar:
sahildeki nokta, basucu ve kuzey yonu, Dunya'nin donus matrisi, Gunes, Ay, gezegenler (Gunes merkezli konum ve hiz),
Samanyolu'nun merkezi ve kutbu. Ciktisi src/scenes/olcek/olcek_bugun.h (uretilmis, git'te); sahne zamani bu andan
itibaren kendi kuralina gore akitir (gezegenler Kepler yorungesiyle).

Eksenler: ICRS (ekvator, J2000). Uzunluklar metre, hizlar m/s. Konumlar geometrik (isik zamani ve sapinim yok: bu bir
fotograf degil, adres).

  python tools/olcek_bugun.py            # baslik dosyasini yazar ve dogrulamalari basar
"""
import math
import os
import sys

import numpy as np
from astropy import units as u
from astropy.coordinates import (EarthLocation, Galactic, Galactocentric, GeocentricTrueEcliptic, ICRS, SkyCoord,
                                 get_body, get_body_barycentric_posvel, solar_system_ephemeris, AltAz)
from astropy.time import Time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "scenes", "olcek", "olcek_bugun.h")

# Sahildeki nokta: Candarli (Dikili), kalenin (38,9338 K, 26,9337 D) ~110 m batisindaki kumsalda suyun kenari (Sentinel-2
# su maskesinin sifir cizgisi, olcek_veri.py kiyi), bati
# koyuna bakar (Sentinel-2, 2025-09-23, 10 m). Ozcan 2026-10-05: "Candarli korfezi [...] Orada kale var ve onun onunde
# sahil var. O sahili kullanabilir misin".
LAT, LON, HEIGHT = 38.93388, 26.93247, 1.0

solar_system_ephemeris.set("de440s")
site = EarthLocation.from_geodetic(lon=LON * u.deg, lat=LAT * u.deg, height=HEIGHT * u.m)


def hour_angle_sun(t):
    """Gunes'in yerel saat acisi (derece, -180..180): 0 = tam ogle."""
    sun = get_body("sun", t, location=site)
    lst = t.sidereal_time("apparent", longitude=site.lon)
    ha = (lst - sun.ra).wrap_at(180 * u.deg)
    return ha.to_value(u.deg)


def solar_noon(day="2026-10-05"):
    a, b = Time(day + "T09:30:00", scale="utc"), Time(day + "T10:30:00", scale="utc")
    fa, fb = hour_angle_sun(a), hour_angle_sun(b)
    assert fa < 0 < fb, (fa, fb)
    for _ in range(40):   # ikiye bolme: saniyenin altina
        m = a + (b - a) / 2
        fm = hour_angle_sun(m)
        if fm < 0:
            a, fa = m, fm
        else:
            b, fb = m, fm
    return a + (b - a) / 2


def gcrs_of(loc, t):
    p, _ = loc.get_gcrs_posvel(t)
    return np.array(p.xyz.to_value(u.m))


def unit(v):
    return v / np.linalg.norm(v)


def angle_deg(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(unit(a), unit(b)))))))


def icrs_dir(coord):
    c = coord.icrs
    ra, dec = c.ra.rad, c.dec.rad
    return np.array([math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec)])


def main():
    t0 = solar_noon()
    print("bugun (gunes oglesi, sahildeki nokta):", t0.utc.isot, "UTC  /", (t0 + 3 * u.hour).utc.isot[11:19], "TSI")

    # sahildeki nokta, basucu, kuzey ve dogu (GCRS ~ ICRS eksenleri, Dunya merkezli)
    B = gcrs_of(site, t0)
    up = unit(gcrs_of(EarthLocation.from_geodetic(LON * u.deg, LAT * u.deg, HEIGHT * u.m + 1000 * u.m), t0) - B)
    north = gcrs_of(EarthLocation.from_geodetic(LON * u.deg, (LAT + 0.01) * u.deg, HEIGHT * u.m), t0) - B
    north = unit(north - up * np.dot(north, up))
    east = np.cross(north, up)

    # Dunya'nin donusu: ITRS eksenlerinin T0'daki GCRS yonleri (sutunlar); donus ekseni ITRS z
    R = np.zeros((3, 3))
    for i in range(3):
        e = np.zeros(3)
        e[i] = 6378137.0
        R[:, i] = unit(gcrs_of(EarthLocation.from_geocentric(*e, unit=u.m), t0))
    # ortonormal yap (sayisal kucuk sapmalar)
    U_, _, Vt = np.linalg.svd(R)
    R = U_ @ Vt

    # Gunes, Ay, gezegenler: barisentrik konum ve hiz farklari (geometrik)
    def pv(name):
        p, v = get_body_barycentric_posvel(name, t0)
        return np.array(p.xyz.to_value(u.m)), np.array(v.xyz.to_value(u.m / u.s))

    sun_p, sun_v = pv("sun")
    earth_p, earth_v = pv("earth")
    moon_p, moon_v = pv("moon")
    planets = {}
    for name in ["mercury", "venus", "earth", "mars", "jupiter", "saturn", "uranus", "neptune"]:
        p, v = pv(name)
        planets[name] = (p - sun_p, v - sun_v)

    # Samanyolu: kuzey galaktik kutup ve merkez yonu (ICRS), Gunes'in merkeze uzakligi ve duzlemden yuksekligi
    ngp = icrs_dir(SkyCoord(l=0 * u.deg, b=90 * u.deg, frame=Galactic))
    gc_dir = icrs_dir(SkyCoord(l=0 * u.deg, b=0 * u.deg, frame=Galactic))
    gcf = Galactocentric()   # astropy 4.0 varsayilanlari: R0 = 8,122 kpc, z_gunes = 20,8 pc (GRAVITY 2018)
    sgra = SkyCoord(ra=266.4168371 * u.deg, dec=-29.0078106 * u.deg, frame=ICRS)
    sgra_dir = icrs_dir(sgra)
    R0 = gcf.galcen_distance.to_value(u.m)
    zsun = gcf.z_sun.to_value(u.m)
    # Gunes'ten merkeze: Sgr A* yonu, R0 uzaklikta (barisentrik ~ Gunes merkezli: fark 1e9 m, bu olcekte sifir)
    gc_from_sun = sgra_dir * R0

    # dogrulamalar (PLAN §3: "galaktik kuzey kutba ~11 derece", "Gunes sistemi ~50 derece egik")
    nep = icrs_dir(SkyCoord(lon=0 * u.deg, lat=90 * u.deg, frame=GeocentricTrueEcliptic(equinox=t0)))
    sun_from_earth = sun_p - earth_p
    altaz = get_body("sun", t0, location=site).transform_to(AltAz(obstime=t0, location=site))
    print(f"  basucu - galaktik kuzey kutup: {angle_deg(up, ngp):.2f} derece")
    print(f"  basucu - ekliptik kutup:       {angle_deg(up, nep):.2f} derece (gezegenlerin yorunge duzlemi bu kadar egik)")
    print(f"  Gunes: yukseklik {altaz.alt.deg:.3f}, azimut {altaz.az.deg:.3f} derece; basucuyla aci {angle_deg(up, sun_from_earth):.3f}")
    print(f"  Gunes uzakligi {np.linalg.norm(sun_from_earth):.6e} m, Ay {np.linalg.norm(moon_p - earth_p):.6e} m")
    print(f"  Ay: basucuyla aci {angle_deg(up, moon_p - earth_p):.2f} derece")
    print(f"  sahildeki nokta Dunya merkezinden {np.linalg.norm(B):.1f} m")
    print(f"  Samanyolu merkezi: R0 {R0:.4e} m, basucuyla aci {angle_deg(up, gc_dir):.2f} derece")

    def v3(v):
        return "{%.17g, %.17g, %.17g}" % tuple(v)

    lines = []
    lines.append("// Generated by tools/olcek_bugun.py (astropy + JPL DE440s): do not edit by hand. Scene 7 \"Olcek\"'s \"today\":")
    lines.append("// 5 October 2026, the true solar noon at the beach point (Candarli, Dikili, Izmir), PLAN §3. ICRS axes; metres, m/s;")
    lines.append("// geometric positions (no light time, no aberration: an address, not a photograph).")
    lines.append("#pragma once")
    lines.append("")
    lines.append("namespace olcek::today {")
    lines.append(f'constexpr const char* kUtc = "{t0.utc.isot}";')
    lines.append(f"constexpr double kJdTdb = {t0.tdb.jd:.10f};")
    lines.append(f"constexpr double kLat = {LAT}, kLon = {LON}, kHeight = {HEIGHT};   // degrees, metres (WGS84)")
    lines.append("// the beach point from the Earth's centre, and its zenith, north and east, at that moment")
    lines.append(f"constexpr double kBeach[3] = {v3(B)};")
    lines.append(f"constexpr double kUp[3] = {v3(up)};")
    lines.append(f"constexpr double kNorth[3] = {v3(north)};")
    lines.append(f"constexpr double kEast[3] = {v3(east)};")
    lines.append("// the Earth's body (ITRS) axes in ICRS at that moment: columns x (Greenwich meridian), y, z (the pole)")
    lines.append("constexpr double kEarthAxes[3][3] = {")
    for r in range(3):
        lines.append("    {%.17g, %.17g, %.17g}," % tuple(R[r]))
    lines.append("};")
    lines.append("constexpr double kEarthOmega = 7.292115146706979e-5;   // rad/s, sidereal, about the pole")
    lines.append("// the Sun and the Moon from the Earth's centre (position, velocity)")
    lines.append(f"constexpr double kSunFromEarth[3] = {v3(sun_p - earth_p)};")
    lines.append(f"constexpr double kMoonFromEarth[3] = {v3(moon_p - earth_p)};")
    lines.append(f"constexpr double kMoonVelFromEarth[3] = {v3(moon_v - earth_v)};")
    lines.append("// the planets (Jupiter and beyond: their systems' barycentres) from the Sun: position, velocity")
    lines.append("struct PV { const char* id; double p[3], v[3]; };")
    lines.append("constexpr PV kPlanets[] = {")
    for name, (p, v) in planets.items():
        lines.append(f'    {{"{name}", {v3(p)}, {v3(v)}}},')
    lines.append("};")
    lines.append("// the Milky Way: its north pole and the direction of its centre (Sgr A*) from the Sun; R0 and the Sun's height")
    lines.append(f"constexpr double kGalNorth[3] = {v3(ngp)};")
    lines.append(f"constexpr double kGalCentreDir[3] = {v3(sgra_dir)};")
    lines.append(f"constexpr double kGalX[3] = {v3(gc_dir)};   // galactic l = 0, b = 0 (the frame's x axis; z = kGalNorth)")
    lines.append(f"constexpr double kGalR0 = {R0:.10e}, kGalZSun = {zsun:.10e};")
    lines.append(f"constexpr double kEclipticNorth[3] = {v3(nep)};")
    lines.append("}   // namespace olcek::today")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("yazildi:", os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    sys.exit(main())
