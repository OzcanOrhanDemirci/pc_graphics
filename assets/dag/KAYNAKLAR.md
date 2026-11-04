# Sahne 13 "Dağ" · veri kaynakları ve lisanslar

Bu klasördeki dosyalar git'e girmez (yalnız bu liste girer). Hepsini `tools/dag_veri.py` indirir ve hazırlar; kaya taramaları
`assets/dag/cc0/`.

| Dosya | Kaynak | Lisans | Ne için |
|---|---|---|---|
| `dem_1s.u16` | Copernicus DEM GLO-30 (92 karo), AWS Open Data `copernicus-dem-30m` | Copernicus DEM lisansı: serbest kullanım, atıf: *© DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved* | Yeryüzünün şekli, 36-46 K, 34-48 D, 1 yay saniyesi |
| `dem_1s.u16`, 16 karo | GLO-30'un herkese açık olmadığı karolarda (Ermenistan, Azerbaycan, sınır karoları; Ağrı dahil): Mapzen Terrain Tiles, Terrarium, zoom 12, AWS Open Data `elevation-tiles-prod` (kaynak SRTM). Düz su hücreleri Copernicus GLO-90'dan (`copernicus-dem-90m`) | Terrain Tiles atfı, `tilezen/joerd` belgesinden: *"United States 3DEP (formerly NED) and global GMTED2010 and SRTM terrain data courtesy of the U.S. Geological Survey."* ve barındırılan hizmet için *"Mapzen"* | Aynı |
| `a0.rgb8`, `a1.rgb8`, `a2.rgb8` | EOX Sentinel-2 cloudless **2016** (`s2cloudless_3857`), WMTS `tiles.maps.eox.at`, 14, 13, 11. yakınlaştırma | CC BY 4.0. Katmanın kendi metni: *EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016) released under Creative Commons Attribution 4.0 International License.* 2018 ve sonrası NC: kullanılmadı | Yerin rengi, 8 m (±33 km), 16 m (±131 km), 64 m (±524 km) |
| `cc0/kayaA_*` | Poly Haven `aerial_rocks_04` (8K, 80 m) | CC0 | Uzaktan kaya dokusu |
| `cc0/kayaB_*` | Poly Haven `gray_rocks` (8K, 1,8 m) | CC0 | Terasın taşları, iri taş alanı |
| `cc0/boulder_01/`, `rock_07/`, `rock_09/` | Poly Haven aynı adlı modeller (8K, 4K, 4K doku) | CC0 | Terasın çevresindeki kayalar; `boulder_01` dokusu dik yamaçlarda da |

Atıf metni (README ve galerinin "Teşekkür"ü): *Elevation: Copernicus DEM GLO-30, © DLR e.V. 2010-2014 and © Airbus Defence
and Space GmbH 2014-2018, provided under COPERNICUS by the European Union and ESA; Mapzen Terrain Tiles, SRTM terrain data courtesy
of the U.S. Geological Survey. Colour: Sentinel-2 cloudless 2016 by EOX IT
Services GmbH (modified Copernicus Sentinel data 2016), CC BY 4.0. Rock: Poly Haven (CC0).*
