# Sahne 7 "Ölçek" · veri kaynakları ve lisanslar

Bu klasördeki dosyalar git'e girmez (yalnız bu liste girer). Hepsini `tools/olcek_veri.py` indirir ve hazırlar;
"bugün"ün konumlarını `tools/olcek_bugun.py` hesaplar (çıktısı `src/scenes/olcek/olcek_bugun.h`, git'te).

| Dosya | Kaynak | Lisans | Ne için |
|---|---|---|---|
| `olcek_bugun.h` (kaynak kodda) | JPL DE440s (astropy 7, `solar_system_ephemeris = de440s`), astropy'nin Galactic ve Galactocentric çerçeveleri | JPL efemerisi kamuya açık; astropy BSD-3 | 5 Ekim 2026 güneş öğlesinde gezegenler, Ay, Güneş, Samanyolu'nun merkezi ve kutbu, sahildeki noktanın başucu |
| `s2_izmir.tex` | Copernicus Sentinel-2 L2A, S2B 2025-09-23, karolar 35SMC ve 35SNC, gerçek renk (TCI); AWS Open Data `sentinel-cogs` (Element 84 Earth Search) | Copernicus Sentinel verisi: serbest, tam ve açık; atıf: "Contains modified Copernicus Sentinel data 2025" | İzmir Körfezi, 10 m, 82 km kare (sahildeki noktanın çevresinde yerel ızgara) |
| `bmng_dunya.tex` | NASA Earth Observatory, Blue Marble Next Generation (Stöckli ve ark. 2005), Ekim 2004, 21600×10800 | NASA görüntüsü, kamu malı | Dünya'nın bulutsuz yüzü |
| `bmng_ege.tex` | Aynı, 500 m karo C1, yerel ızgaraya | Kamu malı | Ege, 500 m, 2048 km kare |
| `ay.tex` | NASA SVS 4720 (CGI Moon Kit), LRO LROC WAC renk mozaiği `lroc_color_poles_8k.tif` | NASA, kamu malı | Ay'ın yüzü |

Atıf metni (README ve galerinin "Ayrıntılar"ında): *Contains modified Copernicus Sentinel data 2025. Blue Marble: NASA Earth
Observatory. Moon: NASA's Scientific Visualization Studio, LRO LROC.*
