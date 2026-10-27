# VARLIK · sahne 11 "Orman", açık lisanslı varlıklar

> Tek kaynak: sahneye giren her hazır varlığın adresi, yazarı, lisansı, ölçüsü, kalite notu ve kararı. Plan `PLAN.md`
> §3.2 (varlık hattı), §3.4 (katmanlar), §3.6 (kompozisyon). İlk tarama 2026-10-27 (O0 alt ajanı): Poly Haven API'sinin
> bütün modelleri, dokuları ve HDRI'ları; ambientCG'nin bütün malzemeleri ve atlasları; Sketchfab'da 102 terim × 2 lisans
> araması ve altı yazarın (PlantCatalog, db4, b_nealie, ffishAsia, The_Structure_World, maxtree) bütün listesi: 5.738
> model. Her aday önce önizlemesiyle, indirilenler ayrıca kendi dokusuyla gözle bakıldı; ölçüler indirilen glTF'ten
> (aşağıdaki "üçgen" ve "boyut" sütunları ölçüm, sayfadaki sayı değil).

## 0 · Lisans ve indirme

- **Lisans kuralı (Özcan):** CC0 ve CC BY (künyeyle) evet; CC BY-SA yalnız başka yol yoksa; **NC ve ND hayır**. Sketchfab'ın
  "Free Standard" lisansı CC değil, alınmaz. Manifestteki 50 varlığın hepsi **CC0** (Poly Haven ve ambientCG): künye
  zorunlu değil ama künye ekranında yine yazılır (§15). CC BY olanlar (Sketchfab) anahtar gelince girer, künyeleri §14'te hazır.
- **İndirme:** `PYTHONIOENCODING=utf-8 python -X utf8 assets/orman/fetch.py` (yeniden çalıştırılabilir, olanı ve özeti
  tutanı atlar). Yeni varlık: `... fetch.py --pin ph:<id>` (Poly Haven, 2K) ya da `--pin acg:<id>` (ambientCG, 2K-JPG zip).
  Dosyalar git'e girmez (`.gitignore`: `assets/orman/cc0/`, `assets/orman/_dl/`); `fetch.py`, `manifest.json` ve bu dosya girer.
- **Manifest = seçilmiş takım.** Yalnız kararı "kullan" olanlar sabitlendi. "Yedek" olanlar aşağıda, gerekirse tek `--pin`
  ile girer; "ele" olanlar bir daha bakılmasın diye gerekçesiyle yazıldı.
- **Disk (2026-10-27 ölçüm):** `cc0/` 1,21 GB (50 varlık), `_dl/` 371 MB (ambientCG zipleri; `fetch.py` özeti zipten
  denetlediği için silinirse bir sonraki çalıştırmada yeniden iner). Bu taramada yeni indirilen ≈1,5 GB.
- **`fetch.py` düzeltmesi (2026-10-27):** Poly Haven'ın çok malzemeli bitkilerinde alfa haritası `Alpha` değil, malzeme başına
  `leaves_alpha` (pachira, jacaranda, tree_small_02) ya da `opacity` adıyla geliyor; betik yalnız `Alpha`'yı alıyordu, bu
  ağaçların yaprakları alfasız kalacaktı. Artık `Alpha`, `*_alpha` ve `opacity` anahtarlarının hepsi `textures/` altına iner.

### Yükleyici için notlar (ölçülerek bulundu)

- **glTF ekseni Y yukarı.** "Boyut" sütunu glTF'in kendi sınır kutusu: genişlik × **boy** × derinlik, metre. Poly Haven
  sayfasındaki `dimensions` (manifestte `dimensions_mm`) bütün varyantları yan yana dizilmiş hâliyle ölçer ve Z yukarıdır:
  tek bitkinin boyu değildir.
- **Varyantlar ayrı kök düğüm.** Çoğu bitki ve kaya takımı birkaç varyantı aynı glTF'te ayrı kök düğüm olarak taşır
  (sayılar tabloda). `pachira_aquatica_01`'de gövde ve yaprak ayrı kök düğüm: `_bark_a` ile `_leaves_a` bir bitkidir (8 düğüm,
  4 bitki).
- **Sayfadaki "polycount" glTF'in üçgen sayısı değil.** Ölçülen: jacaranda 3,86 M (sayfa 312 bin), anthurium 67 bin (sayfa
  4.134), tree_small_02 2,06 M (sayfa 4,65 M), moss_01 204 (sayfa 246 bin: sayfadaki, dağıtılmış Blender sürümü).
- **Alfa:** glTF'teki taban rengi JPG, alfasız; alfa ayrı PNG. Malzemenin `alphaMode`'u güvenilir değil: pachira'nın yaprağı
  `OPAQUE` yazıyor ama `leaves_alpha_2k.png`'si var; jacaranda ve tree_small_02 yaprağı `BLEND` (bizde alfa testi olmalı).
- **Kare olmayan dokular:** `chinese_hackberry_bark` ve `jolcham_oak_bark_01` 2048×4096 (dikey), `Rock057` 2048×1024.
- **ambientCG zipleri** renk, NormalGL ve NormalDX, pürüzlülük, yer değiştirme, AO ve atlaslarda `Opacity` taşır; LeafSet022'de
  ayrıca **`Scattering`** (yaprağın içinden geçen ışık) var. Zipteki `.blend`, `.usdc`, `.tres`, `.mtlx` dosyaları kullanılmaz.

Karar sözlüğü: **kullan** (manifestte, indirildi) · **yedek** (bakıldı, uygun, sabitlenmedi) · **ele** (sahneye zarar verir;
gerekçe yanında).

## 1 · Dev ağaçlar, çatı ve payanda kökler

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `jacaranda_tree` | Poly Haven | Rob Tuytel, Rico Cilliers | CC0 | 24,4 × **19,5** × 19,2 m | 3,86 M | 1 | çatı (yer tutucu) | Tropik/astropik Güney Amerika ağacı; ince tüysü bileşik yaprak, alttan bakınca yağmur ormanı baklagilleri gibi okunur. Alfa çok ince (uzakta merdiven ve kıpırtı riski, O1'de kenar yöntemleriyle denenmeli). Mor çiçek yok, yalnız yaprak. 231 MB, çoğu `.bin` | kullan (geçici: Sketchfab Ceiba ve incir gelince arka halkaya) |

Açık CC0 kaynaklarda **payanda köklü dev gövde yok** (§13). Ele: `fir_tree_01`, `fir_sapling*`, `pine_tree_01`,
`pine_sapling_*` (iğne yapraklı, tropik okumasını bozar); `island_tree_01/02/03` (burgulu gövde, küçük yaprak: zeytin ve
Akdeniz kıyısı görünümü); `quiver_tree_*`, `othonna_cerarioides`, `searsia_*` (Namaqualand çölü).

## 2 · Orta kat ağaçlar ve fidanlar

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `pachira_aquatica_01` | Poly Haven | Rico Cilliers, Rob Tuytel | CC0 | boy 0,58 / 0,85 / 1,0 / 1,65 m | 77 bin | 4 | fidan, alt-orta kat | Gerçek tropik tür (Pachira, beş parmaklı yaprak). Taranmış yaprak temiz, pişmiş gölge yok, alfa temiz | kullan |
| `tree_small_02` | Poly Haven | Rico Cilliers | CC0 | 2,9 × **4,6** × 4,3 m | 2,06 M | 1 | orta kat genç ağaç (yer tutucu) | İnce gövde, yuvarlak sık taç, küçük yaprak: tropik okuması zayıf. Yaprak alfasında UV taşmaları (yakında görünmez). O1 yoğunluk tezgâhı için de gerçekçi ağır parça. 116 MB | kullan (geçici) |

Ele: `island_tree_*` ve iğne yapraklılar (§1). Orta kat için gerçek adaylar Sketchfab'da (§14: palmiyeler, Brugmansia, Ixora).

## 3 · Palmiyeler ve eğrelti ağaçları

Açık CC0 kaynaklarda **model yok**; yalnız gövde dokusu var (§11 `palm_bark`). Adaylar §14'te (PlantCatalog palmiyeleri,
b_nealie "Tree Fern 1").

## 4 · Geniş yapraklı alt kat

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `anthurium_botany_01` | Poly Haven | Rico Cilliers, Rob Tuytel | CC0 | boy 0,14-0,50 m (en büyüğü 0,90 × 0,50 × 1,14) | 67 bin | 6 | alt kat geniş yaprak | Taranmış uzun-oval yaprak, pişmiş gölge yok, alfa temiz (bir yaprakta doğal delikler). Kalp biçimli Anthurium değil, uzun yapraklı tür | kullan |
| `calathea_orbifolia_01` | Poly Haven | Rico Cilliers, Rob Tuytel | CC0 | boy 0,13-0,42 m | 16,7 bin | 5 | alt kat geniş yaprak | Çizgili yuvarlak yaprak, temiz alfa, sapları ayrı şerit. Tropik okuması güçlü | kullan |

Ele: `periwinkle_plant` (pembe çiçekli bahçe bitkisi, kapalı çatı altında yetişmez), `potted_plant_*` (saksılı).
Monstera, muz, heliconia, alocasia, philodendron: açık CC0 kaynakta yok, §14.

## 5 · Eğreltiler, zemin örtüsü, fideler

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `fern_02` | Poly Haven | Rico Cilliers, Rob Tuytel | CC0 | 0,55-0,99 m genişlik, boy 0,21-0,43 m | 6,2 bin | 4 | eğrelti | Taranmış yaprak, pişmiş gölge yok. Alfada birkaç ince sap şeridi var (gövde UV'si), zararsız | kullan |
| `weed_plant_02` | Poly Haven | Rico Cilliers, Rob Tuytel | CC0 | 0,12-0,22 m genişlik, boy 4-7 cm | 11 bin | 5 | zeminde fide, geniş yapraklı ot | Parlak geniş yaprak; doğal sararma ve böcek delikleri (plan §3.2'nin istediği yaşlılık). Alfada sap şeritleri, yaprak kenarı temiz | kullan |

Yedek: `shrub_04` (odunsu küçük fidan, küçük yaprak, 48 bin), `nettle_plant` (dik ot, dişli yaprak; `dry_diff` kuru tonu
var, 46 bin), `shrub_sorrel_01` (oksalis, pembe çiçek). Ele: `shrub_01`, `shrub_03`, `grass_medium_01/02`,
`grass_bermuda_01`, `celandine_01`, `dandelion_01` (ılıman çayır otları; yağmur ormanının alt katında çim yok).

## 6 · Yosun, kök, sarmaşık

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `moss_01` | Poly Haven | Rob Tuytel | CC0 | tek yosun sürgünü, boy 1-4 cm | 204 (toplam) | 12 | yosun saçılması (binlerce kopya; kütük, kaya, gövde dibi) | Alfa kartı (her varyant 16-24 üçgen), `BLEND`. Kart temiz, yaprakçıklar net | kullan |
| `root_cluster_01` | Poly Haven | Jenelle van Heerden, Rico Cilliers | CC0 | 4,1 × 1,5 × 2,7 m | 225 bin | 1 | gövde dibinde kök yığını, topraklı tümsek | Kırmızımsı toprak + kök; tarama, ışık temizlenmiş | kullan |
| `root_cluster_02` | Poly Haven | Jenelle van Heerden | CC0 | 8 parça, en büyüğü 1,45 × 0,19 × 2,28 m | 340 bin | 8 | zemine gömülü yüzey kökleri | Yassı kök parçaları, zemine gömülerek payanda ağacın çevresine yayılır | kullan |
| `single_root` | Poly Haven | Jenelle van Heerden | CC0 | 0,56 × 0,18 × 1,84 m | 61 bin | 1 | tek yüzey kökü | Patikayı kesen kök | kullan |

Sarmaşık ve liana: açık kaynakta kullanılabilir model yok (§13). Kendi yapımımız için yaprak `LeafSet029`, kabuk §11.
Ele: `pine_roots` (çam iğneli zemin).

## 7 · Devrik gövde, kütük, dal, döküntü

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `dead_tree_trunk_02` | Poly Haven | Jenelle van Heerden, Rico Cilliers | CC0 | 4,05 × 1,06 × 1,06 m | 83 bin | 1 | dallı devrik gövde | Kabuğu soyulmuş, çürük; yosun ve mantar yerleştirmesine uygun | kullan |
| `dead_tree_trunk` | Poly Haven | Rob Tuytel | CC0 | 3,05 m boy, çap ~0,29 m | 102 bin | 1 | ince devrik gövde | Koyu, yarıklı kabuk | kullan |
| `tree_stump_01` | Poly Haven | Rob Tuytel | CC0 | 1,43 × 0,57 × 1,59 m | 41 bin | 1 | köklü kütük | Yosun lekeli | kullan |
| `tree_stump_02` | Poly Haven | Rob Tuytel | CC0 | 1,52 × 0,52 × 1,39 m | 62 bin | 1 | kütük | Daha yosunlu | kullan |
| `dry_branches_medium_01` | Poly Haven | Rico Cilliers | CC0 | 0,78 / 1,04 / 1,30 m | 17 bin | 3 | düşmüş dal | Kuru gri dal | kullan |
| `bark_debris_01` | Poly Haven | Greg Zaal, Jenelle van Heerden | CC0 | 4 parça, 0,40-0,61 m | 193 bin | 4 | kabuk döküntüsü | Namaqualand (çöl) koleksiyonundan ama görünüşü evrensel kabuk parçası; ıslaklıkla koyulaşacak | kullan |
| `StickSet001` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | atlas | 6 çubuk | dal ve çubuk kartları | Fotogrametri, opaklık haritası temiz | kullan |

## 8 · Kayalar ve dere taşları

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `boulder_01` | Poly Haven | Rico Cilliers | CC0 | 1,27 × 1,00 × 1,83 m | 66 bin | 1 | iri kaya | Likenli, kızıl-kahve | kullan |
| `rock_moss_set_01` | Poly Haven | Kless Gyzen | CC0 | 6 kaya, 1,8-2,7 m genişlik, boy 1,1-1,8 m | 63 bin | 6 | iri yosunlu kaya | Tek 2K atlas altı kayaya paylaşılmış (atlasın çeyreği boş): doku yoğunluğu düşük, orta ve uzak plan; yakında §11 kaya malzemesiyle karıştırılır | kullan |
| `rock_moss_set_02` | Poly Haven | Kless Gyzen | CC0 | 7 kaya, 1,2-2,2 m, boy 0,5-1,3 m | 58 bin | 7 | yosunlu kaya | Aynı not | kullan |
| `rock_07` | Poly Haven | Jenelle van Heerden | CC0 | 0,17 × 0,14 × 0,32 m | 15 bin | 1 | küçük taş | Koyu demirli taş | kullan |
| `rock_09` | Poly Haven | Jenelle van Heerden | CC0 | 0,07 × 0,03 × 0,14 m | 12 bin | 1 | çakıl | Koyu | kullan |
| `stone_01` | Poly Haven | Dario Barresi, Rico Cilliers | CC0 | 0,15 × 0,07 × 0,09 m | 54 bin | 1 | çakıl | Kuvarslı, pembe-beyaz: renk kalibrasyonunda koyulaştırılmalı | kullan |

Yuvarlak, ıslak dere taşı modeli açık CC0'da yok (yalnız dokusu var: §10 `ganges_river_pebbles`, `Rocks024L`); adaylar §14.
Yedek: `rock_face_01`, `rock_face_02`, `mountainside` (dere kenarı kaya yüzü). Ele: `coast_*`, `coastal_cliff_*`,
`sand_rocks_small_01` (kumsal ve deniz), `namaqualand_*` (çöl), `moon_rock_*`.

## 9 · Yaprak atlasları (döküntü, kart, sarmaşık yaprağı)

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `LeafSet022` | ambientCG | Lennart Demes | CC0 | atlas 25 cm | atlas | 6 yaprak | taze düşmüş yaprak, yaprak kartı | Uzun, düz kenarlı yeşil yaprak (ficus gibi), lekeli; çok açılı çekim (gölgesiz), **`Scattering` haritası var** | kullan |
| `LeafSet023` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | atlas | 6 yaprak | taze yaprak, sarmaşık yaprağı | Parlak açık yeşil ok-kalp biçimi | kullan |
| `LeafSet011` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | atlas | 6 yaprak | kuru döküntü | Kahverengi kuru yaprak, delik ve yırtıklı (eski yöntem: PBRApproximated) | kullan |
| `LeafSet029` | ambientCG | Lennart Demes | CC0 | atlas 25 cm | atlas | 9 yaprak | kendi liana ve sarmaşığımızın yaprağı | Sarmaşık yaprağı, ikisi sararmış; çok açılı çekim | kullan |

Yedek: `LeafSet018`, `LeafSet024` (geniş yeşil), `LeafSet006` (sararmış), `LeafSet008` (soluk kuru). Ele: `LeafSet010`,
`021`, `027`, `028` (akçaağaç), `LeafSet012`, `016`, `030` (meşe), `LeafSet019` ve `PineNeedles001` (iğne), `LeafSet020`
(karahindiba), `Foliage001-008` (çim): ılıman orman imzası.

## 10 · Zemin malzemeleri

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `mud_forest` | Poly Haven | eye-candy.xyz | CC0 | 2,35 m döşeme | doku | 1 | ana zemin: koyu ıslak orman toprağı | Filiz ve yaprak kırıntılı koyu çamur; pişmiş gölge yok | kullan |
| `brown_mud_03` | Poly Haven | Rob Tuytel | CC0 | 1,3 m | doku | 1 | dere kenarı, patika çamuru | Islak, ayak izli, çukurlu | kullan |
| `red_mud_stones` | Poly Haven | Rob Tuytel | CC0 | 1,5 m | doku | 1 | kırmızı (laterit) toprak lekeleri | Tropik toprak rengi; ana zeminle karıştırılır | kullan |
| `dry_decay_leaves` | Poly Haven | Amal Kumar | CC0 | 2,0 m | doku | 1 | sık yaprak döküntüsü | Geniş, düz kenarlı kahverengi yapraklar: tropik döküntüye yakın | kullan |
| `forest_leaves_02` | Poly Haven | Rob Tuytel | CC0 | 3,0 m | doku | 1 | yosunlu yaprak döküntüsü | Yeşil-sarı; ilk takımdan | kullan |
| `Ground048` | ambientCG | Lennart Demes | CC0 | 1,4 m | doku | 1 | koyu humus | Fotogrametri, çok koyu, düz ışık | kullan |
| `Ground023` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | doku | 1 | ıslak kahve çamur, yaprak kırıntılı | Fotogrametri | kullan |
| `Ground068` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | doku | 1 | kırmızı toprak üstünde yosun | Laterit ile yosun geçişi | kullan |
| `ganges_river_pebbles` | Poly Haven | Amal Kumar | CC0 | 2,16 m | doku | 1 | dere yatağı | Yuvarlak, renkli çakıl | kullan |
| `Rocks024L` | ambientCG | Lennart Demes | CC0 | 3,6 m | doku | 1 | dere yatağı (ikinci) | Yassı gri dere taşları | kullan |
| `Moss001` | ambientCG | Lennart Demes | CC0 | 45 cm | doku | 1 | yosun + döküntü örtüsü | Fotogrametri, gerçekçi | kullan |
| `Moss004` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | doku | 1 | koyu yeşil yosun (gövde, kaya, kütük üstü) | Yöntemle üretilmiş; tek başına düz görünebilir, Moss001 ve `moss_01` kartlarıyla birlikte | kullan |

Yedek: `brown_mud_02`, `brown_mud_leaves_01` (kırmızı toprak + yosun), `forest_ground_06`, `forest_leaves_03` (koyu
küçük yapraklar), `dry_river_pebbles`, `Ground037` (yosunlu zemin), `Ground076` (turuncu köklü zemin), `Ground051`
(yöntemle ıslak çamur), `ScatteredLeaves009`, `Gravel018`. Ele: `leaves_forest_ground`, `forest_floor`, `Ground040-042`
(sonbahar yaprakları), `forrest_ground_03`, `forrest_sand_01`, `Ground077` (çam iğnesi), kum, kar, asfalt.

## 11 · Kaya ve kabuk malzemeleri

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `mossy_rock` | Poly Haven | Rob Tuytel | CC0 | 3,0 m | doku | 1 | yosunlu, likenli kaya yüzeyi | Kaya modellerinin yakın plan detayı | kullan |
| `Rock057` | ambientCG | Lennart Demes | CC0 | 2048×1024 | doku | 1 | yosunlu kaya | Canlı yosun yeşili | kullan |
| `Rock053` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | doku | 1 | ıslak koyu dere kayası | Kahve, yosun lekeli, ıslak görünüm | kullan |
| `chinese_hackberry_bark` | Poly Haven | Charlotte Baglioni | CC0 | 1,8 m (doku 2048×4096) | doku | 1 | payanda ağaç ve dev gövdenin düz kabuğu | Düz gri-yeşil, açık liken lekeli: yağmur ormanı ağaçlarının tipik kabuğu | kullan |
| `jolcham_oak_bark_01` | Poly Haven | Charlotte Baglioni | CC0 | 1,0 × 2,0 m (2048×4096) | doku | 1 | yaşlı, yarıklı gövde | Koyu, ıslak görünümlü, yosun izli | kullan |
| `palm_bark` | Poly Haven | Charlotte Baglioni | CC0 | 1,86 m | doku | 1 | palmiye gövdesi | Halkalı koyu gövde (kendi palmiye gövdemiz gerekirse) | kullan |
| `Bark010` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | doku | 1 | düz soluk kabuk | Soluk gri-yeşil, lekeli | kullan |
| `Bark013` | ambientCG | Lennart Demes | CC0 | ölçü verilmemiş | doku | 1 | yosunlu kabuk | Yeşil yosun şeritli | kullan |

Yedek: `lichen_rock`, `Rock063`, `bark_brown_02`, `japanese_hackberry_bark`, `bark_platanus`, `Bark011`, `Bark008`,
`Bark005`. Ele: `pine_bark`, `knotted_pine_bark`, `metasequoia_bark`, `japanese_cedar_bark` (iğne yapraklı kabuğu),
`Rock046L` (granit üstü liken, dağ görünümü), `Rock064` (taş duvar).

## 12 · Gökyüzü ve başvuru HDRI

| id | kaynak | yazar | lisans | gerçek boyut | üçgen | varyant | katman/rol | kalite notu | karar |
|---|---|---|---|---|---|---|---|---|---|
| `kloofendal_28d_misty_puresky` | Poly Haven | Greg Zaal, Jenelle van Heerden | CC0 | 2K equirect | - | 1 | gökyüzü | Puslu sabah, güneş ~28° (adındaki "28d"); plan §3.6'nın 25°'ine en yakın puslu gök. Yalnız gök (yer yok). 2K'da derece başına 5,7 piksel: çatı aralığından net görünecekse 4K/8K'ya geçilir (`fetch.py`'deki `RES`) | kullan |
| `rainforest_trail` | Poly Haven | Dimitrios Savva, Jarod Guest | CC0 | 2K equirect | - | 1 | başvuru (sahnede gök değil) | Gerçek yağmur ormanı içi. Bilinen ışık altında albedo ve renk kalibrasyonu, kör testte "gerçek" ışık örneği | kullan (başvuru) |

Yedek: `kloofendal_misty_morning_puresky` (güneşsiz puslu sabah), `mossy_forest`, `green_sanctuary` (orman içi başvuru).

## 13 · Eksikler (açık CC0 kaynakların kapsamadığı katmanlar)

| katman | durum | yol |
|---|---|---|
| Payanda köklü dev gövde (odak ağaç) | CC0 yok | Sketchfab CC BY: b_nealie "Moreton Bay Fig Tree" taramaları (§14, öncelik 1). Gövde ~3-4 m'de kesik: üstü `chinese_hackberry_bark` ile bizim uzattığımız gövde |
| Çatı ağacı, emergent | Yalnız `jacaranda_tree` (geçici) | Sketchfab "Ceiba Pentandra Tree", "Chinese Banyan" |
| Palmiyeler | Model yok | PlantCatalog palmiyeleri (Butterfly, Bamboo, Alexander, Christmas), "Tropical Plants Pack", maxtree Rhapis |
| Eğrelti ağacı | Yok | b_nealie "Tree Fern 1" |
| Büyük geniş yapraklı (muz, monstera, alocasia, heliconia, taro, philodendron) | Yok | §14; heliconia ve philodendron'da iyi aday yok (en iyisi zayıf: "Heliconia Rostrata") |
| Epifitler (bromelya, orkide, kuş yuvası eğreltisi, gövdeye tutunan eğrelti) | Yok | db4 taramaları (Bird Nest Fern, Flowering Bromeliad), PlantCatalog polypody, ffishAsia CC0 Pyrrosia ve Lepisorus |
| Liana ve sarmaşık iplikleri | Hiçbir kaynakta kullanılabilir yok | Plan §3.2'deki son çare: eğri + `jolcham_oak_bark_01`/`Bark013` + `LeafSet029`/`LeafSet023` yaprak kartları, ip benzetimiyle |
| Mantar (raf mantarı, şapkalı) | Poly Haven ve ambientCG'de yok | Sketchfab CC BY taramaları, ffishAsia CC0 |
| Tohum, meyve | Yok (Poly Haven yiyecekleri pazar meyvesi, ele) | RISD Nature Lab "Cocoa Pod", "Coconut" (CC BY) |
| Yuvarlak ıslak dere taşı (model) | Yalnız doku | Sketchfab "River Rocks Scan", db4 "Waterfall Rock" |
| Tropik yaprak döküntüsü (3B) | Yalnız doku ve atlas | Atlaslardan kendi kartlarımız, düşme benzetimiyle (plan §3.4) |
| Selaginella, yer yosunu öbekleri | Yalnız `moss_01` kartları | ffishAsia CC0 taramaları (çok ağır, 1,3-1,4 M yüz) |

**Diğer açık kaynaklar (bakıldı):** ShareTextures (CC0; doğa modelleri yalnız kütük ve taş), OpenGameArt (CC BY palmiye,
düşük kalite), BlenderKit (ücretsizlerin bir kısmı CC0, hesap ve Blender eklentisi gerekir; denenmedi). Quixel/Fab, Xfrog,
SpeedTree, PlantCatalog'un tam kataloğu, RenderHub: açık lisans değil, ele.

**Anahtarsız yol (yalnız geometri):** Objaverse 1.0 (allenai, Hugging Face) Sketchfab'ın 2022 öncesi CC modellerinin glb
kopyalarını girişsiz verir. kısa listedeki 69 adayın 32'si orada (tabloda "Obj" sütunu). Ama dokular küçültülmüş: üç örnekte
ölçüldü, "Moreton Bay Fig Tree 4" 8K yerine **1024²**, "Banana tree" 4K yerine 1024², "3D Fern - 01" 4K yerine 512×1024.
Yani orada yalnız geometri işe yarar; doku ya özgününden (anahtarla) ya bizim döşemeli kabuk ve yaprak malzemelerimizden.

## 14 · Sketchfab adayları (anahtar gelince)

Arama girişsiz (`api.sketchfab.com/v3/search?type=models&downloadable=true&license=cc0|by`), indirme `SKETCHFAB_TOKEN`
ister (plan §6). Lisanslar arama sonucundan; indirmeden önce model sayfasında bir kez daha bakılır. Yüz sayısı ve doku
Sketchfab'ın bildirdiği (glTF arşivi). Adres: `https://sketchfab.com/3d-models/<uid>`. **Öncelik** sütunu: ilk 10 önce
indirilir. Obj: Objaverse 1.0'da kopyası var (yalnız geometri, yukarı bak).

### 14.1 Dev ağaç, payanda, kök tabağı

| öncelik | uid | ad | yazar (görünen ad) | lisans | yüz | doku | glTF | Obj | neden |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `5bd87cbca118406dabeefb91d1f0aa0a` | Moreton Bay Fig Tree 4 | b_nealie | CC BY 4.0 | 399 bin | 1×8K | 42 MB | var | Payanda kökleri en açık incir taraması; bulutlu ışıkta çekilmiş, sert gölge yok. Odak ağacın dibi |
| - | `7c008a7022874ea3a05654850b8f13df` | Moreton Bay Fig Tree 5 | b_nealie | CC BY 4.0 | 400 bin | 1×8K | 40 MB | var | İkinci incir, yüzey kökleri ve sarkan kökler |
| - | `c8108fd9761c496da981f31a200570ff` | Moreton Bay Fig Tree 2 Rescan | b_nealie | CC BY 4.0 | 399 bin | 1×8K | 52 MB | var | Uzun yayılan yüzey kökleri |
| 2 | `9142b8f06e384f1c9554c9b10da79d2a` | Ceiba Pentandra Tree | Valery.Li | CC BY 4.0 | 1,62 M | 9×2K | 67 MB | var | Tam dev ağaç (kapok), sarkan kökler; çatı ve emergent |
| - | `2a0dbcdf8f5d48f5ad79987c7a8170ce` | Chinese Banyan (Ficus Microcarpa) | Valery.Li | CC BY 4.0 | 111 bin | 17×1K | 8 MB | var | Hava kökleri perdesi |
| - | `6882b88eee884c43b0fbc97f87a9751c` | CC0 Malayan Banyan, Ficus microcarpa | ffishAsia-and-floraZia | **CC0** | 1,64 M | 3×8K | 118 MB | yok | Hava kökleri, CC0 tarama (ağır) |
| 3 | `870b595eda444b4ea23ab9f1db629942` | Uprooted fallen hornbeam tree | matousekfoto | CC BY 4.0 | 601 bin | 1×8K | 54 MB | var | Devrilmiş ağacın kök tabağı (§3.6'nın devrik devi); tür ılıman ama kök tabağı evrensel |
| - | `0d673b77d64f4ad98471fe854bc11f6c` | Tropical Rainforest Tree Stump | db4 (JFN) | CC BY 4.0 | 510 bin | 1×8K | 50 MB | yok | Borneo'da gerçek yağmur ormanı zemini: kök, döküntü, fide. Zemin parçası ve **başvuru** |
| - | `d9bc89b239da4f1c94b28033469d4123` | Raintree Surface Roots 1 | db4 | CC BY 4.0 | 427 bin | 1×8K | 48 MB | yok | Yüzey kökleri (çim ve beton kenarı var, kırpılmalı) |

### 14.2 Orta kat, palmiye, eğrelti ağacı

| öncelik | uid | ad | yazar (görünen ad) | lisans | yüz | doku | glTF | Obj | neden |
|---|---|---|---|---|---|---|---|---|---|
| 4 | `c6bc31d122c043a19346c90f5cbde40e` | Tree Fern 1 | b_nealie | CC BY 4.0 | 130 bin | 3×8K | 29 MB | yok | Gerçek eğrelti ağacı taraması (Yeni Zelanda ponga) |
| 5 | `2f093afb792742438f0f7ba7eaab90f0` | Tropical Plants Pack M02P | mozzarellaARC (MozzarellaARC) | CC BY 4.0 | 47 bin | 15×2K | 25 MB | yok | Palmiye + monstera + eğrelti takımı; oyun kalitesi ama tutarlı |
| 6 | `e25c69a1a9c94aa3a0e3619b8f225a8b` | Realistic HD Butterfly palm (40/62) | PlantCatalog | CC BY 4.0 | 793 bin | 7×8K | 32 MB | yok | Dypsis lutescens, çok gövdeli öbek, 6,6 m. PlantCatalog (e-on) tutarlı kalite |
| - | `3e4abcb771a04fca83074a47c2bb0346` | Realistic HD Bamboo palm (10/30) | PlantCatalog | CC BY 4.0 | 78 bin | 20×1K | 6 MB | yok | Rhapis excelsa: alt kat palmiyesi |
| - | `0a1bb189032b40d8a1f2a9216029fcdd` | Realistic HD Alexander palm (28/30) | PlantCatalog | CC BY 4.0 | 33 bin | 9×8K | 8 MB | yok | İnce gövdeli tek palmiye, orta kat |
| - | `f011c52fae6740a388ac9f7f5db00dd1` | Realistic HD Christmas palm (5/35) | PlantCatalog | CC BY 4.0 | 100 bin | 12×8K | 53 MB | yok | Orta kat palmiyesi |
| - | `155601892ba84ea18abf8ff87cf2d378` | Xishuangbanna- Rhapis_humilis_01 | maxtree | CC BY 4.0 | 436 bin | 12×4K | 78 MB | yok | Xishuangbanna (Çin'in yağmur ormanı) serisi, alt kat palmiyesi |
| - | `ca2832c6d46a4e6ebdc51dd42fbc16b7` | Realistic HD Magnificent angel's trumpet (5/5) | PlantCatalog | CC BY 4.0 | 137 bin | 14×1K | 7 MB | yok | Brugmansia, And bulut ormanı çalısı |
| - | `38916dda0334496c98b3aa19772fb3c7` | Realistic HD Chinese jungle geranium (6/10) | PlantCatalog | CC BY 4.0 | 6,6 bin | 9×2K | 1 MB | yok | Ixora, çiçeksiz çalı |

### 14.3 Geniş yapraklı alt kat

| öncelik | uid | ad | yazar (görünen ad) | lisans | yüz | doku | glTF | Obj | neden |
|---|---|---|---|---|---|---|---|---|---|
| 7 | `3b658ecad29f4d9a9606dbf8fea7c9bb` | Banana tree | 1.Quad (1Quad) | CC BY 4.0 | 48 bin | 1×4K | 6 MB | var | Temiz, gerçekçi muz |
| - | `18925e7161164f6fa4398cc8ab1b716e` | Dwarf Cavendish Banana (Polycam LIDAR) | db4 (JFN) | CC BY 4.0 | 52 bin | 1×4K | 3 MB | var | Gerçek muz taraması (çimli taban kırpılır) |
| - | `3ee280726f1f496e9b2377d43b4cbb2d` | Tropical Plant | alint | CC BY 4.0 | 1,4 bin | 2×1K | 1 MB | var | Monstera, az üçgen ama iyi doku |
| - | `e6a92c1ddb8941e9b8aa92dc1f0f3c18` | Anthurium | thesidekick (Lassi Kaukonen) | CC BY 4.0 | 95 bin | 3×8K | 48 MB | var | Gerçekçi anthurium, kalp yaprak + çiçek |
| - | `09127a0699ea4d1c9246227e19ab452b` | Alocasia macrorrhizos | DJMiddi | CC BY 4.0 | 1,7 bin | 3×2K | 4 MB | var | Dev fil kulağı |
| - | `8b3f41326dc34fad984cafad72f94d2c` | Xishuangbanna- Colocasia_gigantea_01 | maxtree | CC BY 4.0 | 7,6 bin | 9×4K | 65 MB | yok | Dev taro, yağmur ormanı serisi |
| - | `3c9390ddb8c04387afa22241ada26a7b` | Taro | The_Structure_World | CC BY 4.0 | 1,3 bin | 2×4K | 10 MB | var | Fotoğraf dokulu kart bitki |
| - | `4af286ea07c94254a3830de9a56dabbc` | Tropcial Peacock Plant | The_Structure_World | CC BY 4.0 | 280 | 2×4K | 13 MB | var | Calathea makoyana |
| - | `bb7240aecc774cd388304b59dcdc7cdb` | Heliconia Rostrata | maafif426161 (Meownster) | CC BY 4.0 | 38 bin | 9×2K | 30 MB | yok | Tek heliconia adayı, kalitesi orta |

### 14.4 Eğrelti ve epifit

| öncelik | uid | ad | yazar (görünen ad) | lisans | yüz | doku | glTF | Obj | neden |
|---|---|---|---|---|---|---|---|---|---|
| 8 | `ba011757bc9d461f86c3225afd7ba105` | Bird Nest Fern (Scaniverse LIDAR) | db4 (JFN) | CC BY 4.0 | 339 bin | 1×8K | 42 MB | yok | Gerçek kuş yuvası eğreltisi (Asplenium nidus), epifit |
| - | `d67b5816894e4e8780e695ea0e238e01` | Flowering Bromeliad (Scaniverse) | db4 (JFN) | CC BY 4.0 | 152 bin | 1×8K | 28 MB | yok | Çiçekli bromelya taraması |
| - | `0930b42ec567469b8cdeaa93d37c9f06` | BROMELIADS PLANTS | wasis007 | CC BY 4.0 | 22 bin | 1×2K | 4 MB | yok | Kırmızı bromelya rozeti |
| - | `899bc51fa8b6440c87f7e74e60c16ef7` | Realistic HD Common polypody fern (25/55) | PlantCatalog | CC BY 4.0 | 19 bin | 8×4K | 10 MB | yok | Sarkan eğrelti: gövdeye ve kütüğe tutunan |
| - | `165f3870237f488885faf406d9deddc0` | 3D Fern - 01 | SanForge (SanForge Studio) | CC BY 4.0 | 51 bin | 3×4K | 14 MB | var | Gür eğrelti öbeği |
| - | `b052ebcaef59407cbfba0226c5d6dec9` | CC0 Tongue Fern, Pyrrosia lingua | ffishAsia-and-floraZia | **CC0** | 597 bin | 3×4K | 40 MB | yok | Epifit eğrelti, CC0 |
| - | `4e2ac935fe08429cb3b194421786cb13` | CC0 Weeping Fern, L. thunbergianus | ffishAsia-and-floraZia | **CC0** | 383 bin | 3×4K | 29 MB | var | Epifit eğrelti, CC0 |
| - | `069a6240f70749c28910c69ac9e03d8e` | CC0 Japanese Chain Fern, W. japonica | ffishAsia-and-floraZia | **CC0** | 1,23 M | 4×8K | 98 MB | yok | Tek iri yaprak, CC0 |
| - | `37247f0e5b494ca3a1525996175ff215` | Dragon Scale Fern (Scaniverse LIDAR) | db4 (JFN) | CC BY 4.0 | 512 bin | 1×8K | 55 MB | yok | Döküntülü zemin + sürünen eğrelti parçası |

### 14.5 Yosun, kütük, kaya, dal, mantar, tohum, sarmaşık

| öncelik | uid | ad | yazar (görünen ad) | lisans | yüz | doku | glTF | Obj | neden |
|---|---|---|---|---|---|---|---|---|---|
| 9 | `ea0d143472d445cd85ca0d02d2fc2827` | Stump with Shelf Fungus | tharren28 (Tristan Harrenstein) | CC BY 4.0 | 510 bin | 4×4K | 42 MB | var | Raf mantarlı kütük: mantar katmanının tek iyi hazır parçası |
| - | `1b77f05ce1d24f2fbfc34e87ab4f2485` | Shelf Fungus - Photogrammetry | jayden.allen (Jayden Allen) | CC BY 4.0 | 97 bin | 1×8K | 39 MB | var | Tek raf mantarı, gövdeye saçılır |
| - | `50454c79e4e842a181c97a6d1ea7b306` | Tree Fungus (Scaniverse) | db4 | CC BY 4.0 | 142 bin | 1×8K | 20 MB | yok | Gövde parçası üstünde mantar |
| - | `4162cc675bed460ea0274d255a68095b` | CC0 Mushroom | ffishAsia-and-floraZia | **CC0** | 798 bin | 3×4K | 44 MB | var | Şapkalı mantar, CC0 |
| 10 | `c65a00ca4a174653beb4c59cb42b9143` | Mossy old tree log | julianmalik (Julian Malik) | CC BY 4.0 | 20 bin | 3×4K | 24 MB | var | Yosunlu devrik kütük, hafif |
| - | `f91647d2969049329a605095febd1c96` | Mossy tree stump | nedo | CC BY 4.0 | 293 bin | 4×4K | 33 MB | var | Tamamen yosun kaplı kütük |
| - | `86c1a93316e74e79b53a3385eac5012e` | mossy branch FREE | roxyjungle (Roxyjungle) | CC BY 4.0 | 52 bin | 1×4K | 6 MB | var | Yosunlu dal |
| - | `536050e5f80641d98f40ea8b4a5576ab` | Weathered Belian Log | db4 | CC BY 4.0 | 316 bin | 1×8K | 81 MB | var | Borneo demir ağacı kütüğü |
| - | `716b3f558f164053a2c809871c6ff1f3` | Waterfall Rock 5 (Scaniverse) | db4 | CC BY 4.0 | 231 bin | 1×8K | 34 MB | yok | Yosunlu ıslak dere kayası; dikkat: su yüzeyi ve yansıması dokuya gömülü, yalnız sudan yukarısı kullanılır |
| - | `5cb67be08a254e70b89964cfea327dd1` | Moss Rock Photoscan | klessgyzen (Kless) | CC BY 4.0 | 81 bin | 3×4K | 50 MB | var | Yosunlu kaya |
| - | `34276999dfa64f439de965d76f756356` | River Rocks Scan | commonspence (CommonSpence) | CC BY 4.0 | 1,75 M | 1×8K | 58 MB | yok | Yuvarlak dere taşları alanı |
| - | `0ab534c493e14f67bac01ee94f20e43f` | Wood Stick 02 | 3dhdscan | CC BY 4.0 | 30 bin | 3×4K | 48 MB | var | Likenli çubuk |
| - | `d569cdd0d60c43cf9d50165fd3e21ad6` | Branch - Pure Photogrammetry | z3d.nz (Z3D) | CC BY 4.0 | 17 bin | 3×2K | 10 MB | var | İnce düşmüş dallar |
| - | `8657b36b528147f499c26cec5b5e0907` | Cocoa Pod | RISDNaturelab (RISD Nature Lab) | CC BY 4.0 | 500 bin | 1×2K | 23 MB | var | Kakao meyvesi (düşen meyve) |
| - | `b380d4e8e41b4e10b81aa19a0d557434` | Coconut | RISDNaturelab (RISD Nature Lab) | CC BY 4.0 | 1,0 M | 1×2K | 19 MB | var | Kabuklu hindistancevizi |
| - | `f81e6f99cc2745a8958aa96a2c1cf0a2` | CC0 Medicinal Spikemoss, S. involvens | ffishAsia-and-floraZia | **CC0** | 1,42 M | 5×8K | 139 MB | yok | Selaginella, CC0 (ağır, sadeleştirilmeli) |
| - | `33c63d5084324952a5ecbbbecdb2ae53` | CC0 Fern Moss, Thuidium | ffishAsia-and-floraZia | **CC0** | 1,35 M | 2×8K | 96 MB | yok | Eğrelti yosunu öbeği, CC0 |
| - | `91190d6fe4fd44ada32f6156e1d066ab` | Liana tree | JonathanPepe | CC BY 4.0 | 50 bin | 2×8K | 43 MB | var | Birbirine dolanmış gövdeler (boğucu incir görünümü); iplik liana değil |
| - | `d7a591201b904a66a114ebb9347bc6b8` | Realistic Vine plants 3d model free | iGauravRajput | CC BY 4.0 | 18 bin | 1×4K | 8 MB | yok | Sarkan yapraklı sarmaşık perdesi (yedek) |

**Bakılıp elenenler (Sketchfab):** "Realistic Jungle Tree (Avatar)" ve "Jungle Tree" (rodrigogelmi, boypuff): şemsiye taçlı
oyun ağacı, gerçekçi değil. "Port Jackson Fig" (Landy42): iyi payanda ama 7,6 M yüz. "Curtain Fig Tree, Yungaburra"
(kungphil): nokta bulutu, örgü yok. Saksılı bütün monstera, calathea, strelitzia ve pothos'lar (saksı, ev bitkisi
alacalı yaprağı). "Alien Plants kit", stilize ve düşük çokgenli her şey. PlantCatalog'un ılıman türleri (meşe, ladin, kavak).

## 15 · Künye (uygulamadaki künye ekranı için)

Kural: CC BY 4.0 künyesi **başlık, yazar, lisans (bağlantısıyla), kaynak bağlantısı ve değişiklik notu** ister. Bizim
değişikliğimiz her varlıkta aynı: "biçim dönüştürüldü, dokular BC7'ye sıkıştırıldı, gerekirse sadeleştirildi, renk aralığı
ayarlandı". CC0 için künye zorunlu değil; teşekkür olarak aynı biçimde yazılır. Sketchfab'dan bir varlık girdiğinde §14'teki
satırı buraya taşınır (lisans: CC BY 4.0, `https://creativecommons.org/licenses/by/4.0/`).

Lisans bağlantıları: CC0 1.0 `https://creativecommons.org/publicdomain/zero/1.0/` · CC BY 4.0
`https://creativecommons.org/licenses/by/4.0/`.

| başlık | yazar | lisans | kaynak |
|---|---|---|---|
| Anthurium Botany 01 | Rico Cilliers, Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/anthurium_botany_01 |
| Bark Debris 01 | Greg Zaal, Jenelle van Heerden | CC0 1.0 | https://polyhaven.com/a/bark_debris_01 |
| Boulder 01 | Rico Cilliers | CC0 1.0 | https://polyhaven.com/a/boulder_01 |
| Brown Mud 03 | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/brown_mud_03 |
| Calathea Orbifolia 01 | Rico Cilliers, Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/calathea_orbifolia_01 |
| Chinese Hackberry Bark | Charlotte Baglioni | CC0 1.0 | https://polyhaven.com/a/chinese_hackberry_bark |
| Dead Tree Trunk | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/dead_tree_trunk |
| Dead Tree Trunk 02 | Jenelle van Heerden, Rico Cilliers | CC0 1.0 | https://polyhaven.com/a/dead_tree_trunk_02 |
| Dry Branches Medium 01 | Rico Cilliers | CC0 1.0 | https://polyhaven.com/a/dry_branches_medium_01 |
| Dry Decay Leaves | Amal Kumar | CC0 1.0 | https://polyhaven.com/a/dry_decay_leaves |
| Fern 02 | Rico Cilliers, Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/fern_02 |
| Forest Leaves 02 | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/forest_leaves_02 |
| Ganges River Pebbles | Amal Kumar | CC0 1.0 | https://polyhaven.com/a/ganges_river_pebbles |
| Jacaranda Tree | Rico Cilliers, Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/jacaranda_tree |
| Jolcham Oak Bark 01 | Charlotte Baglioni | CC0 1.0 | https://polyhaven.com/a/jolcham_oak_bark_01 |
| Kloofendal 28d Misty (Pure Sky) | Greg Zaal, Jenelle van Heerden | CC0 1.0 | https://polyhaven.com/a/kloofendal_28d_misty_puresky |
| Moss 01 | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/moss_01 |
| Mossy Rock | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/mossy_rock |
| Mud Forest | eye-candy.xyz | CC0 1.0 | https://polyhaven.com/a/mud_forest |
| Pachira Aquatica 01 | Rico Cilliers, Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/pachira_aquatica_01 |
| Palm Bark | Charlotte Baglioni | CC0 1.0 | https://polyhaven.com/a/palm_bark |
| Rainforest Trail (HDRI) | Dimitrios Savva, Jarod Guest | CC0 1.0 | https://polyhaven.com/a/rainforest_trail |
| Red Mud Stones | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/red_mud_stones |
| Rock 07 | Jenelle van Heerden | CC0 1.0 | https://polyhaven.com/a/rock_07 |
| Rock 09 | Jenelle van Heerden | CC0 1.0 | https://polyhaven.com/a/rock_09 |
| Rock Moss Set 01 | Kless Gyzen | CC0 1.0 | https://polyhaven.com/a/rock_moss_set_01 |
| Rock Moss Set 02 | Kless Gyzen | CC0 1.0 | https://polyhaven.com/a/rock_moss_set_02 |
| Root Cluster 01 | Jenelle van Heerden, Rico Cilliers | CC0 1.0 | https://polyhaven.com/a/root_cluster_01 |
| Root Cluster 02 | Jenelle van Heerden | CC0 1.0 | https://polyhaven.com/a/root_cluster_02 |
| Single Root | Jenelle van Heerden | CC0 1.0 | https://polyhaven.com/a/single_root |
| Stone 01 | Dario Barresi, Rico Cilliers | CC0 1.0 | https://polyhaven.com/a/stone_01 |
| Tree Small 02 | Rico Cilliers | CC0 1.0 | https://polyhaven.com/a/tree_small_02 |
| Tree Stump 01 | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/tree_stump_01 |
| Tree Stump 02 | Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/tree_stump_02 |
| Weed Plant 02 | Rico Cilliers, Rob Tuytel | CC0 1.0 | https://polyhaven.com/a/weed_plant_02 |
| Bark 010, Bark 013 | Lennart Demes (ambientCG) | CC0 1.0 | https://ambientcg.com/a/Bark010 · https://ambientcg.com/a/Bark013 |
| Ground 023, Ground 048, Ground 068 | Lennart Demes (ambientCG) | CC0 1.0 | https://ambientcg.com/a/Ground023 · https://ambientcg.com/a/Ground048 · https://ambientcg.com/a/Ground068 |
| Leaf Set 011, 022, 023, 029 | Lennart Demes (ambientCG) | CC0 1.0 | https://ambientcg.com/a/LeafSet011 · https://ambientcg.com/a/LeafSet022 · https://ambientcg.com/a/LeafSet023 · https://ambientcg.com/a/LeafSet029 |
| Moss 001, Moss 004 | Lennart Demes (ambientCG) | CC0 1.0 | https://ambientcg.com/a/Moss001 · https://ambientcg.com/a/Moss004 |
| Rock 053, Rock 057, Rocks 024 L | Lennart Demes (ambientCG) | CC0 1.0 | https://ambientcg.com/a/Rock053 · https://ambientcg.com/a/Rock057 · https://ambientcg.com/a/Rocks024L |
| Stick Set 001 | Lennart Demes (ambientCG) | CC0 1.0 | https://ambientcg.com/a/StickSet001 |

Künye ekranı manifestten de üretilebilir: her girdide `name`, `authors`, `license`, `page` var (CC BY girdileri eklendiğinde
aynı alanlar doldurulur).
