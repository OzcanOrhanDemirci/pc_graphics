# Third-party data and assets

pc_graphics itself is under the MIT licence ([LICENSE](LICENSE)). The data and assets it uses keep their own licences.
Most of them are not committed: the scripts named below fetch them, check their checksums and put them where the
scenes read them. What is committed is marked so.

## Committed

| What | Where | Source | Licence |
| --- | --- | --- | --- |
| IBM Plex fonts | `assets/fonts/` | IBM | SIL Open Font License 1.1 (`assets/fonts/LICENSE.txt`) |
| Amphora profiles (scene 5) | `src/scenes/koy/koy_amphora.cpp` | ArchAIDE consortium (2019), vector profiles of the University of Southampton's drawings (P. Copeland, *Roman Amphorae: a digital resource*, 2014), https://doi.org/10.5284/1050896 | CC BY 4.0 |
| CIE colour-matching functions and illuminants | `src/shared/spectrum.h` and the spectral scenes (1, 5, 7, 12) | CIE 018:2019 and CIE datasets (doi 10.25039/CIE.DS.xvudnb9b) | CC BY-SA 4.0 |
| Blue-noise texture | `assets/bluenoise128.png` | made here (`tools/bluenoise.py`) | MIT |

## Fetched (not committed)

| Scene | Script | Sources | Licence |
| --- | --- | --- | --- |
| 3 · Collapse | `assets/deprem/fetch.py` | Earthquake acceleration records: ESM, the Engineering Strong-Motion database (Mascandola et al., doi 10.13127/esm.3), four records fetched from its web service; AFAD TADAS, Disaster and Emergency Management Presidency of Türkiye, the 2020 Bayraklı record, downloaded by hand after TADAS's guest login (the script says how); original network TK (doi 10.7914/SN/TK) | not stated for redistribution, so they are fetched from the source; cite as given |
| 5 · The Island | `assets/koy/fetch.py` | Poly Haven, ambientCG (`assets/koy/LICENSES.md`) | CC0 |
| 7 · Scale | `tools/olcek_veri.py` | Copernicus Sentinel-2; NASA Blue Marble, GIBS (MODIS, VIIRS), LRO; JPL DE440; Minor Planet Center; HYG v4.1; 2MASS Redshift Survey; Gaia DR3; ESO/S. Brunier (`assets/olcek/KAYNAKLAR.md`) | public domain, free with credit, HYG CC BY-SA 4.0, ESO CC BY 4.0 |
| 11 · Rainforest | `assets/orman/fetch.py` | Poly Haven, ambientCG (`docs/orman/VARLIK.md`, `assets/orman/manifest.json`) | CC0 |
| 12 · Fire | `assets/ates/fetch.py` | Poly Haven (`docs/ates/VARLIK.md`, `assets/ates/manifest.json`) | CC0 |
| 13 · Mountain | `tools/dag_veri.py` | Copernicus DEM GLO-30, Mapzen Terrain Tiles, EOX Sentinel-2 cloudless 2016, Poly Haven (`assets/dag/KAYNAKLAR.md`) | Copernicus DEM licence, CC BY 4.0, CC0 |
| all | `third_party/fetch.sh` | NVIDIA CUDA redistributables, OptiX SDK headers, DLSS SDK; GLFW; stb; tinyexr; cgltf; Khronos GL headers | NVIDIA's licences, zlib, public domain, BSD, MIT, MIT |

## Credits that pictures in this repository carry

Pictures of scene 7 show the Earth (NASA Blue Marble, NASA GIBS, Copernicus Sentinel-2), the stars (HYG database v4.1,
CC BY-SA 4.0, by David Nash, astronexus) and the Milky Way (ESO/S. Brunier, CC BY 4.0). Pictures of scene 13 show
terrain and colour from Copernicus DEM GLO-30 (© DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018,
provided under COPERNICUS by the European Union and ESA) and Sentinel-2 cloudless 2016 by EOX IT Services GmbH
(modified Copernicus Sentinel data 2016, CC BY 4.0).
