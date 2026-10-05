# Sahne 7 "Ölçek" · veri kaynakları ve lisanslar

Bu klasördeki dosyalar git'e girmez (yalnız bu liste girer). Hepsini `tools/olcek_veri.py` indirir ve hazırlar;
"bugün"ün konumlarını `tools/olcek_bugun.py` hesaplar (çıktısı `src/scenes/olcek/olcek_bugun.h`, git'te).

| Dosya | Kaynak | Lisans | Ne için |
|---|---|---|---|
| `olcek_bugun.h` (kaynak kodda) | JPL DE440s (astropy 7, `solar_system_ephemeris = de440s`), astropy'nin Galactic ve Galactocentric çerçeveleri | JPL efemerisi kamuya açık; astropy BSD-3 | 5 Ekim 2026 güneş öğlesinde gezegenler, Ay, Güneş, Samanyolu'nun merkezi ve kutbu, sahildeki noktanın başucu |
| `s2_ev.tex` | Copernicus Sentinel-2 L2A, S2B 2025-09-23, karolar 35SMD, 35SMC, 35SNC, 35SND, gerçek renk (TCI); AWS Open Data `sentinel-cogs` (Element 84 Earth Search) | Copernicus Sentinel verisi: serbest, tam ve açık; atıf: "Contains modified Copernicus Sentinel data 2025" | Çandarlı ve çevresi, 10 m, 82 km kare (sahildeki noktanın çevresinde yerel ızgara); 2025-09-23, karolar STAC ile bulunur |
| `bmng_dunya.tex` | NASA Earth Observatory, Blue Marble Next Generation (Stöckli ve ark. 2005), Ekim 2004, 21600×10800 | NASA görüntüsü, kamu malı | Dünya'nın bulutsuz yüzü |
| `bmng_ege.tex` | Aynı, 500 m karo C1, yerel ızgaraya | Kamu malı | Ege, 500 m, 2048 km kare |
| `bmng_kusak.tex` | Blue Marble'ın her enlemdeki ortalaması (doğrusal ışıkta) | Kamu malı | Hızla dönen Dünya'nın kuşakları |
| `kiyi.sdf` | `s2_ev.tex`'in su maskesinden denizin işaretli uzaklığı (4 km, 2 m) | Copernicus verisinden türetilmiş | Kumsalın gerçek kıyıya oturması |
| `bulut_ege.tex` | NASA GIBS (Worldview), MODIS Terra Corrected Reflectance True Color, **2026-10-05**, ev çevresi | NASA EOSDIS GIBS: kısıtsız kullanım, atıf | Bugünün bulutları (bulutsuz Blue Marble'a göre fark) |
| `bulut_dunya.tex` | NASA GIBS, VIIRS NOAA-20 Corrected Reflectance True Color ve VIIRS NOAA-20 Cloud Optical Thickness (bulut maskesi: deniz parıltısını ayıklar), **2026-10-03** (ikisinin birlikte bulunduğu en yakın gün; ürün ~2 gün gecikmeli, araç yeniden koşunca ilerler) | Aynı | Dünyanın bulutları |
| `yildizlar.bin` | HYG Database v4.1 (Hipparcos, Yale Bright Star, Gliese; astronexus, David Nash) | CC BY-SA 4.0 | Uzaklığı bilinen 109.400 yıldızın gerçek yeri, ışığı (mutlak kadirden), rengi (B-V'den, Ballesteros 2012) |
| `galaksiler.bin` | 2MASS Redshift Survey (Huchra ve ark. 2012, ApJS 199, 26; VizieR J/ApJS/199/26) | Bilimsel katalog, atıfla serbest | 43.490 galaksinin gerçek yönü, kırmızıya kaymadan uzaklığı (H0 = 70), K bandından ışığı |
| `kusaklar.txt` | Minor Planet Center, MPCORB.DAT (2026-10-05 baskısı, 1.570.259 yörünge) | IAU MPC verisi, atıfla serbest | Ana kuşak ve Kuiper kuşağı halkaları (yörüngelerin zaman ortalamalı uzaklık ve yükseklik dağılımı), Jüpiter'in Truvalı kümeleri (L4/L5 sayıları ve yayılımları) |
| `ag.bin` | Bu projede üretildi: Gauss rastgele alan (BBKS tayfı) + Zel'dovich yaklaşımı, 256³, 600 Mpc | Kendi verimiz | Anketin ötesinde kozmik ağın istatistiksel bir örneği |
| `ay.tex` | NASA SVS 4720 (CGI Moon Kit), LRO LROC WAC renk mozaiği `lroc_color_poles_8k.tif` | NASA, kamu malı | Ay'ın yüzü |
| `cie1931_2deg.csv` | CIE 1931 2° standart gözlemcinin renk eşleme fonksiyonları, CIE 018:2019 Tablo 6 (DOI 10.25039/CIE.DS.xvudnb9b), `files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv`, değiştirilmeden, sha256 sabitli | CC BY-SA 4.0 (atıf: *CIE 2019, Colour-matching functions of CIE 1931 standard colorimetric observer, International Commission on Illumination (CIE), Vienna*) | Hız: her sıcaklıkta kara cismin rengi ve görünür payı; Doppler'le kayan her yıldızın ışığı |
| (kaynak kodda) Proxima Centauri | Gaia DR3 5853498713190525696 (Gaia Collaboration, Vallenari ve ark. 2023): J2016.0 konumu, paralaks 768,0665 mas, öz hareket ve dikine hız; astropy ile 2026-10-05'e taşındı | Gaia verisi, atıfla serbest | Hız: Proxima'nın bugünkü yeri (1,301729 pc); kataloğun (HYG) Proxima'sı buraya taşınır |

Atıf metni (README ve galerinin "Ayrıntılar"ında): *Contains modified Copernicus Sentinel data 2025. Blue Marble: NASA Earth
Observatory. Clouds: NASA Worldview / GIBS (MODIS Terra, VIIRS NOAA-20 true colour and cloud optical thickness). Stars: HYG Database v4.1 (CC BY-SA 4.0). Proxima Centauri: Gaia DR3 (ESA/Gaia/DPAC). Colour: CIE 1931 2° CMFs (CIE 2019, CC BY-SA 4.0). Galaxies: 2MASS Redshift Survey (Huchra et al. 2012). Asteroids and the Kuiper belt: Minor Planet Center (MPCORB). Milky Way arms: Reid et
al. 2019. Moon: NASA's Scientific Visualization Studio,
LRO LROC.*
