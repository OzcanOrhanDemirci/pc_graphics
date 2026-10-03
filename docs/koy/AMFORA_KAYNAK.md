# AMFORA KAYNAK · sahne 5 "Amfora", A0 kaynak araştırması

> Amfora geometrisi, amfora ve kemer malzemesi, deniz kabuğu katmanı için bulunan, ölçülen ve seçilen kaynaklar.
> 2026-10-04, A0 alt ajanı (kod yazılmadı, kaynak koda ve git'e dokunulmadı). İndirilen dosyalar yalnız oturumun geçici
> klasöründe: `%USERPROFILE%\AppData\Local\Temp\claude\C--Users-%USERNAME%\050265b6-0aa5-4cbf-a8e3-2c6f1601756d\scratchpad\amfora_a0\`
> (oturumla silinebilir; kalıcı adresler ve SHA-256 değerleri aşağıda). Lisans kuralı: model ve doku için yalnız CC0 ya da
> CC BY (NC ve ND yok). Gerçek fotoğraflar `BASVURU.md` §12-13'te. Hedef: kamera 30 cm'den bakarken tekstel ≤ 0,2 mm,
> yani 8K bir doku en çok ≈1,64 m'lik alanı kaplamalı (16K ise ≈3,28 m).

## 0 · Kısa sonuç

1. **Geometri: döner yüzey (tarama değil).** Kaynak: ArchAIDE'in Southampton çizimlerinden çıkardığı vektör profiller (CC BY
   4.0, birim mm). Girişsiz indirdiğim üç taramanın üçünün de dokusu 1024² (1 m'lik nesnede ≈2,4-2,7 mm tekstel, hedefin
   12-13 katı; tek istisna kara buluntusu ve ölçeksiz bir kırık gövde), hiçbiri gerçek ölçekte değil, girişsiz inen hiçbir
   tarama Dressel 1B değil ve ışık dokuya pişmiş. Profiller ise çizimle 1-2 mm içinde örtüşüyor (bağımsız ölçtüm), boyutları
   Southampton aralıklarında ya da en çok ≈1 cm dışında, 1B gerçek bir 1B ile (MARQ, 118 cm) aynı boyda.
2. **Doku setleri:** gövde kili Poly Haven `clay_plaster` (2,00 m; 16K'da 0,122 mm, 8K'da 0,244 mm); kabuk ve konkresyon
   katmanı Poly Haven `coral_fort_wall_01` (1,36 m; 8K'da 0,166 mm); kemer ambientCG `Rock043S` (0,90 m; 8K'da 0,110 mm)
   ya da zaten bellekte olan `rock_boulder_dry` 16K (1,80 m; 0,110 mm). Kabuk canlıları için CC0 doku yok: kodla, ölçüleri §3.3'te.
3. **Profiller:** dört tip (Dressel 1B, 1A, Dressel 2-4 İtalyan, Lamboglia 2), her biri 28 noktalık tablo (§2.4), kulplar
   (§2.5), tam çözünürlüklü CSV'ler geçici klasörde. Güvenilirlik: her tablo tek bir çizilmiş örnek (tip ortalaması değil);
   sadeleştirmenin profilden en büyük sapması 1,2-2,2 mm.

## 1 · Amfora taramaları (A1, A2)

### 1.1 Nerelere bakıldı

- **Smithsonian 3D** (3d-api.si.edu): "amphora" ve "amphorae" aramaları 0 sonuç. Open Access API'de "amphora" 930 kayıt,
  ilk 100'ünde 3B medya yok. Uygun aday yok.
- **Zenodo** (API): "amphora AND (photogrammetry OR 3D OR scan OR model)" 66 kayıt. Taşıma amforası olanlar 3dbigdataspace
  topluluğunun Objaverse/Sketchfab kopyaları (CC BY 4.0, glb, girişsiz; örnek kayıt 10364667, aşağıdaki 7. aday). Geri
  kalanı CC BY-NC-SA Europeana aktarımları ya da yayınlar.
- **Europeana** (API, TYPE:3D): 54 kayıt; açık lisanslı (CC0, CC BY) olanlar POLIMI 3D-ICONS'un süslü Yunan ve Apulya
  amforaları (3D-PDF, 23-51 cm) ve Eureka3D'den bir boyun parçası. Taşıma amforası taraması yok.
- **Sketchfab** (API, giriş yok): 12 arama, 740 indirilebilir model; adı amfora olan ve CC0/CC BY lisanslı 180 model; Roma
  taşıma amforası ya da batık bağlamlı olanlar aşağıda. İndirme Sketchfab'da hesap istiyor. Görüntüleyicinin doku listesi
  (`sketchfab.com/i/models/<uid>/textures`) girişsiz okunuyor: tablodaki doku çözünürlükleri oradan.
- **Objaverse 1.0** (allenai, Hugging Face): Sketchfab'daki CC lisanslı modellerin 2022'de toplanmış glb kopyaları, girişsiz
  (kimlik listesi `object-paths.json.gz` ile tek tek denetlendi). Tablodaki adaylardan 2022 öncesi yüklenenlerin hepsi burada;
  2022 Kasım ve sonrası yüklenenler yok. **İndirdiğim üç kopyanın üçünde de doku 1024²'ye inmiş** (Sketchfab'daki özgünleri 4096²).
- **MicroPasts** (Southampton çizimlerinden kalabalık sayısallaştırma, CC BY): yalnız A-B harfli tipler (Africana, Almagro,
  Agora); Dressel ve Lamboglia yok. Proje sunucusu 2026-10-04'te 500 hatası verdi.
- **ArchAIDE** (ADS, CC BY 4.0): Southampton çizimlerinin vektör profilleri (SVG) ve bunlardan döndürülmüş OBJ'ler. Tarama
  değil ama aranan tiplerin hepsi var: §2.
- **Scan the World** (MyMiniFactory) ve **Open Heritage 3D**: amfora bulunamadı (web araması; ikincisi site ölçekli veri).

### 1.2 Aday tablosu

Doku sütunu Sketchfab görüntüleyicisinin listesindeki en büyük görüntü. "Objaverse" = girişsiz glb kopyası var (doku 1024²'ye
inmiş olabilir; indirdiklerimde öyle). Gerçek boy: kaynakta yazmıyorsa "yok".

| # | Model | Yazar | Lisans | Tip (kaynağın sözü) | Gerçek boy | Doku | Üçgen | Girişsiz | Durum |
|---|---|---|---|---|---|---|---|---|---|
| 1 | [Ánfora DR.7-11 del pecio Bou Ferrer](https://sketchfab.com/3d-models/a6d3fe261ef944aea7b7bea5db1bbf5b) | Pecio Bou Ferrer (jamoya) | CC BY 4.0 | Dressel 7-11, Bou Ferrer batığı, Vilamuseu | yok | 1 × 4096² | 107.655 | evet, Objaverse (indirildi, §1.3) | tam; bir yanı kalın konkresyonlu |
| 2 | [Ánfora Dressel 20 parva](https://sketchfab.com/3d-models/b419273dfd2c4255ba94c17bf9e08887) | José A. Moya | CC BY 4.0 | Dressel 20 parva (Bou Ferrer projesi) | yok | 1 × 4096² | 241.143 | evet, Objaverse | tam; gri-beyaz konkresyon lekeli |
| 3 | [Amph 941](https://sketchfab.com/3d-models/3c4379b6684b4e37bd7ceb09bd4ad105) | Robyn | CC BY 4.0 | yazmıyor; küresel gövde (silüetten), metal sehpa ağa dahil | yok | 1 × 4096² | 2.800.633 | evet, Objaverse (indirildi) | tam görünüyor; beyaz kabuk izleri |
| 4 | [Amph 951](https://sketchfab.com/3d-models/9dee04e5abd140b496eb383ccef46924) | Robyn | CC BY 4.0 | yazmıyor; küresel gövde, halka sehpada | yok | 1 × 4096² | 2.318.912 | evet, Objaverse | açıklama: deniz dibinde yattığı yerde konkresyon sınırı |
| 5 | [Amphora (Antiochia ad Cragum)](https://sketchfab.com/3d-models/684f3bf937314852a44feee0e3ee318c) | Ben Kreimer | CC BY 4.0 | yazmıyor; akropol kazısı 2019 (kara) | yok | 94 × 4096² | 3.959.574 | evet, Objaverse (indirildi) | kırık, gövdede büyük eksikler |
| 6 | [Ancient amphore KAS/Turkey](https://sketchfab.com/3d-models/6482ab461dd84a1da716ad304e2dfc2e) | holger.buss (dive3d.eu) | CC BY 4.0 | yazmıyor; Kaş "Assi Island" dalış noktası, yerinde su altı taraması | yok | 1 × 4096² | 89.003 | evet, Objaverse | dip ile birlikte, kayalar arasında |
| 7 | [Roman Anchor and Amphora in Cirkewwa](https://sketchfab.com/3d-models/6b985b20baa64e7fab72ea11633a7731) | Emanuele (ema.ottaviani) | CC BY 4.0 | replika amfora ve çapa, ~30 m | yok | 4 × 4096² | 299.908 | evet, Objaverse ve [Zenodo 10364667](https://zenodo.org/records/10364667) | dip sahnesi |
| 8 | [Ánfora romana.](https://sketchfab.com/3d-models/75d0d66a46024c46a5838bb2921d7bd7) | J. Víctor Febles | CC BY 4.0 | Ampurias amforasının modern kopyası | yok | 1 × 4096² | 263.196 | evet, Objaverse | tam (kopya) |
| 9 | [Marine-Encrusted Amphora, Lefkada](https://sketchfab.com/3d-models/4cf47b5d96b949f5bb1a12205676ed95) | iedu360.eu | CC BY 4.0 | "Dressel 2-4" (yükleyici); önizlemede kısa boyunlu, yuvarlak kulplu: iddia görünüşle tutmuyor | yok | 1 × 8192² | 419.843 | hayır, giriş gerekir | kabuklu (beyaz-pembe balanus tabanları); sehpa ve arka plan parçası ağda |
| 10 | [Encrusted Transport Amphora](https://sketchfab.com/3d-models/8d3009c701ba4a68af3b2578646c5682) | iedu360.eu | CC BY 4.0 | yazmıyor; ovoid gövde, düz ağız | yok | 1 × 8192² | 267.857 | hayır | tam, tanecikli kabuk |
| 11 | [Anfora 1 Murallas Reales](https://sketchfab.com/3d-models/e7a82e5531f04e82968d20635c9f7f7a) | patrimonioculturalceuta | CC BY 4.0 | yazmıyor | yok | 1 × 4332×4244 | 2.500.025 | hayır | tam, yoğun mavi-gri konkresyon |
| 12 | [Anfora tipo Dresel I](https://sketchfab.com/3d-models/55bab099e8ca4e7c89b6d0617130a426) | SparKing300 | CC BY 4.0 | Dressel 1 (yükleyici; Vasto açıkları) | yok | 9 × 1024² | 33.826 | hayır | restore, sehpada |
| 13 | [Anfora Dressel 2-4 Menofilo](https://sketchfab.com/3d-models/d04f49e2adc5466baee8b96b3460fb9f) | DSSBC_Siena | CC BY 4.0 | Dressel 2-4 (damgalı parça) | yok | 1 × 4096² | 764.576 | hayır | yalnız parça |
| 14 | [Ánfora (GEMINA 3D)](https://sketchfab.com/3d-models/6734a0695ce54608a1760c032fe57a43) | Tantaramusa | CC BY 4.0 | "ánfora romana", Guadalquivir vadisi | 115 cm (model açıklamasındaki fiş) | 1 × 2048² | 307.927 | hayır | tam |
| 15 | [Ánfora (Haltern 70)](https://sketchfab.com/3d-models/ce5d4f5b9d7848dcb3b97f35325a3f19) | Concello de Vigo, Museo Quiñones de León | CC BY 4.0 | Haltern 70 | yok | 7 × 1024² | 49.506 | hayır | tam |
| 16 | [Amfora scoici](https://sketchfab.com/3d-models/d2b8e885896949ecab6aa8d65c8f252b) | nJo-nJo | CC BY 4.0 | yazmıyor ("kabuklu amfora") | yok | 2 × 2048² | 1.500.000 | hayır | kabuklu |
| 17 | [Amphora Stack_Hellenistic](https://sketchfab.com/3d-models/c1341d457d014f74a3e74df23d3734c4) | NomadArch | CC BY 4.0 | Helenistik yığın, 2 m su, güney Türkiye | yok | liste boş | 1.202.912 | hayır | yerinde yığın |
| 18 | [Ancient amphora](https://sketchfab.com/3d-models/4ecc68b47eb14c428fca95efb951df4a) | StefanoGaboli | CC BY 4.0 | küçük konik kap (taşıma amforası değil) | yok | 1 × 8192² | 1.421.314 | hayır | tam |
| 19 | [Repro Dressel 1 amphora](https://sketchfab.com/3d-models/7669863d727d458ca2f05bd293ceaf65) | MarkArch | **CC BY-NC-SA: kural dışı** | Dressel 1 kopyası | yok | 1 × 8192² | 95.586 | hayır | elendi (NC) |
| 20 | MicroPasts "(ADS DRxx)" serisi, örn. [Africana 3A](https://sketchfab.com/3d-models/c85c6d7906284bef91aac91124d657f4) | MicroPasts | CC BY 4.0 | çizimden döndürülmüş (tarama değil), yalnız A-B tipleri | çizimden | doku yok | 35-190 bin | evet, Objaverse | Dressel yok |
| 21 | [ArchAIDE OBJ, örn. Dressel_1_DR105](https://archaeologydataservice.ac.uk/archiveDS/archiveDownload?t=arch-3369-1/dissemination/archaide_ceramics/obj/Dressel_1_DR105.obj) | ArchAIDE consortium (ISTI-CNR) | CC BY 4.0 | çizimden döndürülmüş, dokusuz | 1179,5 mm (ölçtüm) | yok | 252.074 | **evet, doğrudan** | tam, kulplu, iç yüzlü |

Elenenler: aramaların döndürdüğü 740 modelden 175'i CC BY-NC, BY-NC-SA ya da BY-NC-ND, 17'si "Free Standard" lisanslı
(lisanstan elendi); adı amfora olan CC0/CC BY modellerin geri kalanı süslü Yunan amforaları, süs vazoları ya da düşük çokgenli
oyun varlıkları (addan ve önizlemeden elendi).

### 1.3 İndirilen üç aday ve incelemesi

Seçim ölçütü: girişsiz, tam ya da öğretici (kırık), en yüksek ayrıntı. Hepsi Objaverse 1.0'dan (Hugging Face), SHA-256 değeri
Hugging Face'in `X-Linked-ETag` başlığıyla (bağımsız kaynak) aynı. glb dosyalarını kendi ayrıştırıcımla okudum (trimesh yok).

| | DR 7-11 Bou Ferrer | Amph 941 | Amphora (Ben Kreimer) |
|---|---|---|---|
| Dosya | `dl/a6d3fe261ef944aea7b7bea5db1bbf5b.glb` | `dl/3c4379b6684b4e37bd7ceb09bd4ad105.glb` | `dl/684f3bf937314852a44feee0e3ee318c.glb` |
| Adres | huggingface.co/datasets/allenai/objaverse/resolve/main/glbs/000-061/... | .../glbs/000-008/... | .../glbs/000-071/... |
| Bayt | 4.512.076 | 92.608.860 | 176.261.256 |
| SHA-256 | cfd9ebd30816da85bf9118513344536b6c29853ac9c72cc6f707844d77ca992e | 1d4f70002e079fb37a6e6b4504cc46567c55d00245aed838f98c504d43097560 | 5225247ead19279b01287ad9b9f8bb484d87774649ae80b4adb37d9385836902 |
| Köşe / üçgen | 59.050 / 107.655 | 1.819.489 / 2.800.633 (çok parçaya bölünmüş) | 2.983.541 / 3.959.570 |
| Malzeme | 1, specular-glossiness, köşe rengi var | 1, ışıksız (unlit) | 94, ışıksız, UDIM benzeri u1-u10 × v1-v10 |
| Doku (bu kopya) | 1 × 1024² JPEG | 1 × 1024² JPEG | 94 × 1024² JPEG |
| Doku (Sketchfab özgünü) | 1 × 4096² | 1 × 4096² | 94 × 4096² |
| UV kullanımı | atlasın %13,4'ü | %38,5 | 94 karoda medyan %72 (en az %7, en çok %82) |
| Sınır kutusu | 2,06 × 5,76 × 2,18 birim | 8,26 × 8,93 × 9,13 birim | 10,56 × 31,75 × 10,54 birim |
| Gerçek ölçek | hayır (birimsiz, ölçek yazmıyor) | hayır | hayır |
| Tekstel (alan ağırlıklı medyan) | en uzun boyutun 2,37·10⁻³'ü: nesne 1 m ise 2,4 mm | 2,68·10⁻³: 2,7 mm | 1,34·10⁻⁴: 0,13 mm |
| Biçim (köşelerin ana eksen izdüşümü) | tam amfora; boy:en 1:0,37 | küresel gövde 1:0,98; sehpa çubukları ağda | silindir 1:0,44; gövdede büyük delikler |

Tekstel nasıl ölçüldü: her üçgen için √(dünya alanı / (UV alanı × doku eni × boyu)), üçgen alanıyla ağırlıklı medyan; birimsiz
olduğu için "en uzun boyut 1 m olsaydı" diye ölçeklendi.

Dokuda gözle gördüklerim (atlas ve 512 px kırpıntılar, 1:1):
- **DR 7-11:** atlasın çoğu siyah dolgu; gri adacıklarda ten rengi konkresyon kümeleri ve beyaz bir etiket; ışık yumuşak, gölge
  pişmiş olabilir; 1K'da piksel ≈2,4 mm, ayrıntı yok.
- **Amph 941:** pembe-turuncu pişmiş toprak üzerinde çok sayıda beyaz halka ve nokta (serpulid ya da balanus taban izleri) ve
  ince beyaz iplik izleri; adacıklar arasında koyu kırmızı-kahve ton farkları: ışık ve nem dokuya pişmiş; 1K'da bulanık.
  Kabuk izlerinin yerleşimi ve rengi için iyi referans.
- **Ben Kreimer:** dış yüz açık gri, ince taneli ayrıntı 1K karoda bile keskin; çatlak çizgileri; kırık ve iç yüzler kırmızı-kahve;
  derin iç neredeyse siyah (ortam kapanması dokuya pişmiş); karo kenarlarında bulanık dolgu; deniz kabuğu yok (kara kazısı).
  Kırık kenar ve iç yüz rengi için referans.

Sonuç: üçü de sahneye doğrudan girmez: doku 1K (hedefin 12-13 katı kaba; Ben Kreimer hariç, o da kara amforası ve ölçeksiz),
tip uymuyor, ölçek yok, ışık pişmiş. Özgün 4K dokular bile (1K ölçüsünün dörtte biri, aynı UV düzeniyle) ≈0,6 mm verir.

## 2 · Tipoloji profilleri (A3)

### 2.1 Kaynak ve lisans

- **Southampton, "Roman Amphorae: a digital resource"** (University of Southampton 2014, ADS, https://doi.org/10.5284/1028192):
  tip sayfaları, karakteristikler (boy, en, ağız çapı aralıkları), Penny Copeland'in 1:10 çizimleri (600 dpi JPEG, 50 cm ölçek
  çubuğu). Lisans sayfada: "Data copyright © University of Southampton unless otherwise stated. This work is licensed under the
  ADS Terms of Use and Access" (CC değil): depoya çizim girmez, yalnız sayılar. Canlı site Cloudflare doğrulaması istediği için
  sayfa metinleri Wayback kopyalarından okundu; çizim dosyaları ADS'nin `archiveDS` adresinden doğrudan indi.
- **ArchAIDE** ("ARCHAIDE Portal for Publications and Outputs", ArchAIDE consortium 2019, ADS, https://doi.org/10.5284/1050896):
  Southampton çizimlerinin ISTI-CNR "ArchAIDE Digit Tool" ile çıkarılmış vektörleri. Lisans sayfada: "This work is licensed under
  a Creative Commons Attribution 4.0 International License" (Wayback 2022-02-21 kopyası). SVG'de adlı yollar var
  (`outer_base_profile`, `inner_base_profile`, `outer_handle_profile`, `inner_handle_profile`, `handle_section`, `axis`),
  koordinatlar mm (dosyada `ratio_mm_pixels: 0.42337` = 600 dpi ve 1:10'un tam karşılığı), y aşağı doğru.
- **Hangi çizim hangi tip** (Southampton çizim sayfasının kendi yazısından): DR104 "Form 1A. After Benoit, 1961"; DR105 "Form
  1B. After Peacock, 1971"; DR106 "Form 1C. After Charlin et al, 1978"; hepsi "Drawing at 1:10 scale", Penny Copeland.
  DR139, DR188, DR191: Dressel 2-4 Italian; DR136: Lamboglia 2.
- Adresler: `https://archaeologydataservice.ac.uk/archiveDS/archiveDownload?t=arch-3369-1/dissemination/archaide_ceramics/svg/<ad>.svg`
  (OBJ için `.../obj/<ad>.obj`); ham çizim `...?t=arch-463-2/dissemination/jpg/drawings_orig/DR105.jpg`.

SHA-256 (2026-10-04'te indirilen; ADS özet vermiyor, ilk indirmede sabitlendi):

| Dosya | SHA-256 |
|---|---|
| Dressel_1_DR105.svg (1B) | bb784c89a51d82387788ba10858f398d3dcb63f00d2a66a36d69ba43d89a35da |
| Dressel_1_DR104.svg (1A) | 86eed40f41f54899557317da28bdf82a1ff2d727365f06def64034f2c8c6f51f |
| Dressel_1_DR106.svg (1C) | e5930a1218785ddaf75bc356c1bbbec7d835547034e463324e9d435101578bca |
| Dressel_2-4_Italian_DR139.svg | c1b1f9faf8bd4fb35ef32cac0d8a17690d70b63022dd16b46a7825d0d00af5d3 |
| Lamboglia_2_DR136.svg | d231abf3dbf7c41406b6b6569335d5be4eb516aea9c0f5e0910bb8ba83d44897 |
| Dressel_1_DR105.obj | 658dd2967ab54790c291cd619c8cdb6d8c13e24af7babb1b473006a1fc3c220a |
| DR105.jpg (ham çizim) | c379cba4e7a88d22cfea2f452590c6f81d964fe537ae1119258d06414a86acf4 |

### 2.2 Nasıl okundu ve nasıl doğrulandı

- **Ölçek:** sekiz ham çizimin hepsinde 50 cm çubuğu çerçevesiyle birlikte 1184 px; 600 dpi ve 1:10'da 50 cm = 1181,1 px, fark
  %0,25 (çerçeve çizgisinin kalınlığı kadar). ArchAIDE'in mm/px oranı bununla aynı.
- **Bağımsız sayısallaştırma:** ham çizimi kendim okudum (eksen çizgisi = en uzun düşey siyah koşu; kesit tarafında eksenden
  dışa doğru ilk siyah koşunun dış ucu = dış yarıçap) ve ArchAIDE dış profiliyle 25 yükseklikte kıyasladım. Kulp bağlantı
  satırları, ağız üstü ve dip ucu dışında medyan |fark|: 1B 1,28 mm (sistematik -1,3 mm; büyük olasılıkla eksen çizgisinin kalınlığından), 1A 0,01
  mm, 1C 1,70 mm, 2-4 (DR139) 0,01 mm, 2-4 (DR188) 1,27 mm, Lamboglia 2 2,12 mm. Farkın büyüdüğü yerler: kulp bağlantısı (ham
  çizimde kulp gövdeye yapışık, ArchAIDE boynu altından izliyor), ağız üstü, dip ucu.
- **Görsel denetim:** ArchAIDE profillerini ham çizimin üstüne çizip baktım (1B, 2-4, Lamboglia 2): dış profil hem kesit hem dış
  görünüş tarafında çizgiyle çakışıyor; iç profil ve kulplar yerinde.
- **Gerçek nesneyle:** MARQ (Alicante) Tossal de la Cala Dressel 1B'si: "h:118 cm", "w. 32 cm", "26/27 litres" (müze sayfası,
  https://www.marqalicante.com/en/?p=16390); DR105: 118,0 cm, 29,0 cm, hesap hacmi 22,1 L.
- **Hacim denetimi:** iç profili döndürerek hesapladığım Dressel 2-4 (DR139) hacmi 20,7 L; Southampton'un "Capacity (average)"
  değeri 20-25 litres.
- **ArchAIDE OBJ (DR105):** sınır kutusu 291,4 × 1179,5 × 291,4 mm (SVG: en 290,4, boy 1179,1); 484 mm yükseklikte iç yüz
  yarıçapı 123,0, dış 145,5 mm (duvar 22,5 mm; SVG'den 24,1). Dosyada 126.081 köşe, 252.074 üçgen; ADS'nin metadata CSV'si bu
  dosya için 496.465 köşe yazıyor (uyuşmuyor, not).

### 2.3 Tip özeti

Southampton aralıkları "characteristics" sayfalarından (Dressel 1: id 324, Dressel 2-4 Italian: id 102, Lamboglia 2: id 229).
Duvar: gövdede yatay kalınlık (dış eksi iç yarıçap, toplam yüksekliğin %30, %45 ve %60'ında; omuza denk gelen ölçüm hariç). Hacim: iç profilin
döndürülmesi, boşluk dibinden ağız üstüne.

| Tip (çizim) | Boy (cm) | En büyük çap (cm), yüksekliği | Ağız dış / iç çap (cm) | Boyun en dar dış çap (cm), yüksekliği | Duvar (mm) | Hacim (L) | Southampton |
|---|---|---|---|---|---|---|---|
| **Dressel 1B (DR105)** | 118,0 | 29,0 @ 48,4 | 17,8 / 14,9 | 11,1 @ 83,5 | 23-25 | 22,1 | boy 97-117, en 28-29, ağız 17-18 |
| Dressel 1A (DR104) | 97,8 | 28,2 @ 42,6 | 17,6 / 15,2 | 10,8 @ 70,7 | 13-15 | 21,3 | aynı (Dressel 1 tek sayfa) |
| Dressel 1C (DR106) | 118,0 | 29,1 @ 58,3 | 14,9 / 11,0 | 10,6 @ 94,4 | 14-16 | 28,8 | aynı |
| **Dressel 2-4 İtalyan (DR139)** | 94,4 | 26,7 @ 51,3 | 14,9 / 12,1 | 9,2 @ 75,0 | 12-13 | 20,7 | boy 95-108, en 26-29, ağız 14-16, 20-25 L |
| Dressel 2-4 İtalyan (DR191) | 108,3 | 29,4 @ 48,8 | 16,1 / 13,0 | 12,8 @ 86,8 | 15-20 | 29,0 | aynı |
| Dressel 2-4 İtalyan (DR188) | 85,8 | 32,9 @ 42,6 | 11,9 / 9,6 | 10,9 @ 83,0 | 16-18 | 27,7 | aynı (boy aralığın altında) |
| **Lamboglia 2 (DR136)** | 85,5 | 40,8 @ 27,9 | 15,9 / 14,6 | 10,0 @ 71,6 | 13,5-17 | 44,0 | en 41-42, ağız 16-17 (boy "0.00cm": yok) |
| Dressel 20 (D_001, kıyas için) | 80,4 | 54,3 @ 35,6 | 16,6 / 10,9 | 10,9 @ 75,6 | 15-18 | 82,2 | (bakılmadı) |

Tarih uyumu (Southampton "Date Range"): Dressel 1A ≈140/130'dan MÖ 1. yy ortasına; 1B MÖ 2. yy son çeyreğinden MÖ 1. yy son
on yılına; İtalyan Dressel 2-4 ≈MÖ 70'ten; Lamboglia 2 MÖ 2. yy'dan MÖ 1. yy'ın son üçte birine. Dördü ≈MÖ 70-50 arasında aynı
anda dolaşımda (1A'nın bitişi sınırlıyor; 1A çıkarılırsa üçü ≈MÖ 70-30). Dressel 1 duvarı çizimden (1B için 23-25 mm, 1A ve 1C için 13-16 mm): gerçek örneklerle kıyaslanmadı.

### 2.4 Profil tabloları (döner yüzey için)

ArchAIDE dış profilinin (ağızdan dibe sıralı poliline) Douglas-Peucker ile 28 noktaya sadeleştirilmişi, dipten ağıza yazıldı.
z: dipten yükseklik; r dış: döndürülecek dış yarıçap; aynı z'de iki satır yatay bir basamaktır (dip altı, ağız üstü). r iç: aynı
yükseklikteki iç (boşluk) yüzeyi; omuz ve ağızda duvar eğik olduğu için kalınlık bu farktan okunmaz. "-": o yükseklikte boşluk
yok (dolu dip). Başlıktaki sapma, sadeleştirmenin tam profilden en çok ne kadar uzaklaştığı.

#### Dressel 1B (DR105) · 28 nokta, en büyük sapma 2,2 mm

| z (cm, dipten) | r dış (cm) | r iç (cm) |
|---:|---:|---:|
| 0,0 | 0,00 | - |
| 0,0 | 3,05 | - |
| 1,0 | 3,73 | - |
| 6,8 | 2,84 | - |
| 12,7 | 3,56 | - |
| 21,6 | 6,18 | 1,52 |
| 26,7 | 8,21 | 6,05 |
| 29,4 | 9,65 | 7,41 |
| 37,4 | 12,70 | 10,33 |
| 42,4 | 13,93 | 11,73 |
| 46,7 | 14,44 | 12,32 |
| 53,0 | 14,52 | 12,07 |
| 62,7 | 13,89 | 11,43 |
| 74,3 | 13,67 | 9,48 |
| 79,1 | 7,11 | 4,87 |
| 80,7 | 6,10 | 4,40 |
| 82,3 | 5,63 | 4,36 |
| 85,7 | 5,59 | 4,57 |
| 92,2 | 6,27 | 4,61 |
| 101,2 | 5,93 | 4,40 |
| 104,2 | 6,82 | 4,70 |
| 109,6 | 6,77 | 5,21 |
| 111,0 | 7,41 | 5,59 |
| 112,1 | 8,89 | 6,22 |
| 114,9 | 8,34 | 6,77 |
| 117,2 | 8,85 | 6,69 |
| 117,8 | 8,47 | 7,03 |
| 117,9 | 7,45 | 7,45 |

Okuma: dip ucu düz tabanlı ve dolu (z 0-21 cm), en büyük çap z ≈53 cm'de, omuzda keskin kırılma (z 74,3 → 79,1), boyun z 80-110,
dik yaka ağız z 111-118 (yaka ≈7 cm, Southampton'un "thick near vertical collar rim" tanımı).

#### Dressel 1A (DR104) · 28 nokta, en büyük sapma 1,2 mm

| z (cm, dipten) | r dış (cm) | r iç (cm) |
|---:|---:|---:|
| 0,0 | 0,00 | - |
| 0,1 | 2,50 | - |
| 0,5 | 3,05 | - |
| 4,5 | 3,18 | - |
| 9,7 | 4,78 | - |
| 11,0 | 5,50 | - |
| 15,5 | 8,89 | 5,80 |
| 19,1 | 10,71 | 8,09 |
| 26,0 | 12,70 | 10,67 |
| 34,4 | 13,67 | 12,11 |
| 42,6 | 14,10 | 12,74 |
| 52,7 | 14,06 | 12,62 |
| 57,6 | 13,80 | 10,33 |
| 58,5 | 13,25 | 8,17 |
| 61,0 | 8,55 | 5,88 |
| 62,0 | 7,54 | 5,46 |
| 64,7 | 6,22 | 4,87 |
| 67,9 | 5,59 | 4,61 |
| 70,7 | 5,38 | 4,40 |
| 79,0 | 5,42 | 4,11 |
| 88,1 | 6,05 | 5,00 |
| 88,7 | 6,35 | 5,08 |
| 93,2 | 7,49 | 5,84 |
| 93,3 | 8,38 | 5,84 |
| 93,9 | 8,81 | 6,05 |
| 97,2 | 8,38 | 7,28 |
| 97,7 | 7,96 | 7,58 |
| 97,8 | 7,62 | 7,62 |

Okuma: üçgen kesitli, eğik ağız (z 93-98, ≈4,5 cm), daha kısa gövde ve boyun.

#### Dressel 2-4 İtalyan (DR139) · 28 nokta, en büyük sapma 1,5 mm

| z (cm, dipten) | r dış (cm) | r iç (cm) |
|---:|---:|---:|
| 0,0 | 0,00 | - |
| 0,3 | 1,91 | - |
| 0,7 | 2,58 | - |
| 1,6 | 3,30 | - |
| 4,1 | 3,18 | - |
| 5,9 | 2,71 | 0,97 |
| 9,0 | 2,96 | 1,57 |
| 12,3 | 3,98 | 2,75 |
| 21,1 | 8,04 | 6,60 |
| 27,8 | 10,41 | 9,19 |
| 35,7 | 12,19 | 11,05 |
| 44,0 | 13,08 | 11,81 |
| 51,3 | 13,34 | 12,11 |
| 60,5 | 13,29 | 12,02 |
| 64,2 | 13,17 | 11,81 |
| 67,7 | 12,70 | 10,03 |
| 69,3 | 11,09 | 7,07 |
| 72,0 | 6,14 | 4,36 |
| 72,8 | 5,42 | 3,94 |
| 74,9 | 4,61 | 3,60 |
| 80,2 | 4,83 | 3,68 |
| 84,4 | 5,38 | 4,32 |
| 87,3 | 6,14 | 4,78 |
| 92,0 | 6,65 | 5,55 |
| 93,1 | 7,45 | 5,50 |
| 93,7 | 7,37 | 5,55 |
| 94,1 | 7,03 | 5,80 |
| 94,3 | 6,05 | 6,05 |

Okuma: dipte küçük topuz, ince duvarlı silindirik gövde, omuz keskin, boyun yukarı doğru genişleyen kum saati, yuvarlak dudak.

#### Lamboglia 2 (DR136) · 28 nokta, en büyük sapma 1,7 mm

| z (cm, dipten) | r dış (cm) | r iç (cm) |
|---:|---:|---:|
| 0,0 | 0,00 | - |
| 0,5 | 2,16 | - |
| 3,7 | 3,90 | - |
| 6,0 | 5,80 | 0,76 |
| 6,8 | 6,90 | 3,09 |
| 9,5 | 12,28 | 8,76 |
| 13,1 | 15,92 | 13,42 |
| 15,3 | 17,49 | 15,28 |
| 17,8 | 18,63 | 16,81 |
| 21,2 | 19,69 | 18,16 |
| 24,5 | 20,19 | 18,84 |
| 31,2 | 20,41 | 19,05 |
| 37,5 | 19,81 | 18,37 |
| 46,0 | 18,25 | 16,77 |
| 53,7 | 16,13 | 13,42 |
| 54,8 | 15,41 | 10,84 |
| 55,7 | 12,83 | 9,40 |
| 58,1 | 8,76 | 6,60 |
| 59,9 | 7,11 | 5,50 |
| 62,4 | 6,14 | 4,91 |
| 69,3 | 5,12 | 4,02 |
| 75,1 | 5,00 | 3,73 |
| 80,1 | 5,76 | 4,45 |
| 81,2 | 6,73 | 4,95 |
| 81,5 | 7,37 | 5,08 |
| 82,3 | 7,66 | 5,42 |
| 84,9 | 7,96 | 6,69 |
| 85,5 | 7,28 | 7,28 |

Okuma: kısa sivri dip, torba gibi geniş gövde (en büyük çap aşağıda, z ≈31), omuzda kırılma, uzun silindirik boyun, sarkık
üçgen-kare ağız.

### 2.5 Kulplar

Profil düzleminde (eksenden yarıçap) kulpun dış kenarı ve iç kenarı (boyuna bakan) ArchAIDE'in kulp yollarından; kesit,
`handle_section` yolunun sınır kutusu. Kesit çizimlerde farklı yönlerde döndürülmüş: profildeki radyal kalınlıkla eşleşen kenar
radyal, öteki teğet kabul edildi (çıkarım).

| Tip | Dış kenar z (alt-üst, cm) | Boşluk z (cm) | Dış kenar r, serbest kısım (cm) | En dış nokta (serbest kısım) | İç kenar r (cm) | Radyal kalınlık (cm) | Kesit (radyal × teğet, mm) | Boyun r, aynı z (cm) | Kulp-boyun boşluğu (cm) |
|---|---|---|---|---|---|---|---|---|---|
| Dressel 1B (DR105) | 75,4-108,9 | 79,1-105,2 | 11,5-11,9 | r ≈11,8 @ z 84-92 (kulp neredeyse dik: z 80-100 boyunca 11,6-11,8; alt bağlantıda 11,9) | 8,2-8,5 | ≈3,3-3,4 | ≈33 × 54 (kutu 54,2 × 33,4) | 5,6-6,3 | ≈2,2-2,9 |
| Dressel 1A (DR104) | 58,8-93,0 | 60,8-88,9 | 10,8-11,4 | r 11,4 @ z ≈63 | 7,3-7,9 | ≈3,3 | ≈31 × 54 (kutu 31,3 × 53,8) | 5,4-5,8 | ≈1,9-2,1 |
| Dressel 2-4 (DR139) | 69,3-91,3 | 71,9-87,9 | 10,5-11,6 | r 11,6 @ z ≈86 (yukarı doğru dışa açılan dirsek) | 7,9-8,8 | ≈2,8-2,9 | çift çubuk ("8" biçimi), ≈33 × 52 (kutu 52,1 × 33,0) | 4,7-5,6 | ≈3,2 |
| Lamboglia 2 (DR136) | 55,7-79,5 | 57,4-75,7 | 12,9-13,2 | r 13,2 @ z ≈62 | 9,3-10,3 | ≈3,3-3,5 | neredeyse yuvarlak oval, 35,1 × 32,6 | 5,0-6,4 | ≈4,3-4,6 |

Kulplar gövdeden dışarı taşmaz (Dressel 1'de en dış kulp yarıçapı ≈12 cm, gövde 14,5 cm); alt bağlantı omuzda, üst bağlantı
ağzın hemen altında.

### 2.6 Geçici klasördeki dosyalar (`prof/`)

- `svg/*.svg`: ArchAIDE vektörleri (DR104, DR105, DR106, DR139, DR188, DR191, D_001, DR136, Dressel_20_D_001).
- `<ad>_outer_polyline_mm.csv`, `<ad>_inner_polyline_mm.csv`: tam çözünürlüklü sıralı poliline (z, r mm; ≈0,42 mm adım).
- `<ad>_outer_handle_profile_mm.csv`, `<ad>_inner_handle_profile_mm.csv`, `<ad>_handle_section_mm.csv`: kulp yolları.
- `<ad>_simplified.csv`: §2.4 tabloları. `DR*.jpg`: ham çizimler. `Dressel_1_DR105.obj`: ArchAIDE'in döndürülmüş ağı.
- `overlay_check.jpg`: profillerin çizim üstünde görsel denetimi. Betikler: `svgparse.py`, `prof_svg.py`, `prof_raster.py`,
  `prof_table.py`, `prof_metrics.py`, `prof_handle.py`.

## 3 · Malzeme (B)

Fiziksel boyut: Poly Haven API'de `dimensions` alanı, kendi tanımıyla "the dimensions of this asset on each axis in millimeters";
ambientCG'de `dimensionX/Y` (birimi API belgesinde yazmıyor; Ground054 için 350 = VERI.md'deki 3,5 m ile tutarlı, yani cm).
Tekstel = boyut / çözünürlük. Önizlemeler (küre ve düz render) açılıp bakıldı; doku setleri indirilmedi. Boyutlar API'deki dosya
boyutları (Poly Haven: Diffuse, nor_gl, Rough, Displacement, AO JPG toplamı; ambientCG: 8K-JPG zip).

### 3.1 Pişmiş toprak ve kil (B4)

| Set | Kaynak | Boyut | 8K tekstel | 16K tekstel | Haritalar | İndirme boyutu | Gözle |
|---|---|---|---|---|---|---|---|
| **clay_plaster** | Poly Haven (Amal Kumar) | 2,00 m | 0,244 mm ✗ | 0,122 mm ✓ | Diffuse, nor_gl, Rough, Displacement, AO (+arm, nor_dx) | 8K ≈127 MB, 16K ≈455 MB | düz, ince taneli, hafif mala dalgalı kil sıva; renk okra-kahve (terracotta'dan sarı); çömlek yüzüne en yakın ölçülü set |
| patterned_clay_wall | Poly Haven | 1,25 m | 0,153 mm ✓ | yok | 7 harita | 8K ≈258 MB (7 harita) | yumru desenli kil duvar: amforaya uymaz |
| terracotta_floor_tiles | Poly Haven (Dimitrios Savva) | 2,08 m | 0,254 mm ✗ | yok | 7 harita | 8K ≈164 MB (5 harita) | gerçek pişmiş toprak ama derz çizgili karolar |
| brown_floor_tiles | Poly Haven | 1,70 m | 0,208 mm ✗ (sınırda) | yok | | | açık kahve karo, derzli |
| brick_4 | Poly Haven (Rob Tuytel) | 0,50 m | 0,061 mm ✓ | yok | 9 harita | 8K ≈55 MB | ıslak kırmızı tuğla, harç ve yosun: derz yüzünden uymaz |
| medieval_red_brick | Poly Haven | 2,00 m | 0,244 mm ✗ | 0,122 mm ✓ | 7 harita | | tuğla duvar, harçlı |
| clay_block_wall, clay_floor_001 | Poly Haven | 2,00 m | 0,244 mm ✗ | 0,122 mm ✓ | (bakılmadı) | | çatlaklı kerpiç blok; boyalı düz zemin |
| roof_tiles | Poly Haven | 2,00 m | 0,244 mm ✗ | 0,122 mm ✓ | | | kiremit sırası |
| Clay001, **Clay002**, Clay003 | ambientCG | **yazmıyor** (API'de 0) | hesaplanamaz | yok | standart set (Color, NormalGL/DX, Roughness, Displacement, AO); zip içerik listesi API'de boş | 8K-JPG zip 407 / 441 / 418 MB | düz, ince dikey çizgili pembe-turuncu kil: görünüşte çömlek yüzüne en benzeyen; boyut bilinmeden tekstel bilinmez |
| Clay004 | ambientCG | yazmıyor | | | | 410 MB | gri kil, renk uymaz |

Ad ve etiketten elenenler (önizleme açılmadı): ambientCG GlazedTerracotta001-003 (sırlı, amfora sırsızdır; boyut yazmıyor),
RoofingTiles011-015 (kiremit, 2,90 m → 8K'da 0,354 mm). "ceramic" ve "pottery" aramaları ambientCG'de 0 sonuç. Poly Haven'da çömlek
taraması olarak yalnız modeller var (planter_pot_clay, potted_plant_01/02, ceramic_vase_01-04): dokusu o modele açılmış, döşenemez.

### 3.2 Kemer için kireçtaşı ve karst kaya (B5)

| Set | Kaynak | Boyut | 8K tekstel | 16K tekstel | İndirme boyutu | Gözle |
|---|---|---|---|---|---|---|
| **Rock043S** | ambientCG | 0,90 m | 0,110 mm ✓ | yok | 8K-JPG zip ≈391 MB | gri, gözenekli, beyaz ve yeşil lekeli, çatlaklı: karst kireçtaşına en benzeyen |
| Rock043L | ambientCG | 1,80 m | 0,220 mm ✗ | yok | ≈405 MB | aynı taşın büyük ölçeklisi (uzak yüzler, tekrar kırıcı) |
| Rock042S | ambientCG | 1,20 m | 0,146 mm ✓ | yok | ≈290 MB | bej, aşınmış, düzgün kireçtaşı |
| Rock046S | ambientCG | 0,90 m | 0,110 mm ✓ | yok | ≈381 MB | beyaz kaya üstünde yeşil yosun lekeleri (kara kayası) |
| Rock044 | ambientCG | 1,15 m | 0,140 mm ✓ | yok | ≈370 MB | yeşilimsi-ten, kaba |
| **coral_fort_wall_01** | Poly Haven (Dimitrios Savva, Rico Cilliers) | 1,36 m | 0,166 mm ✓ | 0,083 mm | 8K ≈218 MB, 16K ≈701 MB | açık gri, gözenekli, kabuk kırıntılı biyojenik kireçtaşı ("mercan taşı" kale duvarı, derz görünmüyor): su altı kayasına ve amfora konkresyonuna en benzeyen |
| rock_08 | Poly Haven (Rob Tuytel) | 1,50 m | 0,183 mm ✓ | yok | 8K ≈124 MB | soluk bej-gri, küçük koyu çukurlu |
| rock_2 | Poly Haven (Rob Tuytel) | 1,50 m | 0,183 mm ✓ | yok | 8K ≈128 MB | bej, koyu benekli |
| rock_3 | Poly Haven (Rob Tuytel) | 1,50 m | 0,183 mm ✓ | yok | (ölçülmedi) | gri-kahve, koyu gözenekli |
| **rock_boulder_dry** | Poly Haven (depoda zaten var: `assets/koy`) | 1,80 m | 0,220 mm ✗ | 0,110 mm ✓ | 8K ≈195 MB, 16K ≈609 MB | açık, kireçtaşı görünümlü; Koy'da 16K yüklü |

Boyutu yazmayan beyaz kireçtaşı görünümlüler (ambientCG Rock017, Rock018, Rock019, Rock026): önizlemede tebeşirimsi ve
katmanlı, uygun görünüyor ama tekstel hesaplanamıyor.

### 3.3 Deniz kabuğu kaplaması (B6): CC0 doku yok

ambientCG'de "barnacle", "coral", "seaweed", "algae", "encrusted" aramaları 0 sonuç; "shell" yalnız Ground060, Ground056 (kabuk
kırıntılı zemin) ve ShellSet001 (kabuk modeli). Poly Haven'da yalnız zemin dokuları (shell_floor_01 3 m, coral_gravel,
coral_ground_02, coral_mud_01 2 m) ve tek kabuk modeli (lambis_shell). Serpulid, balanus, bryozoa ya da kireçli alg kaplaması
için CC0 doku bulunamadı: kodla üretilecek. Gerçek ölçüler ve renkler:

| Canlı | Biçim | Ölçü | Renk | Nerede | Kaynak |
|---|---|---|---|---|---|
| Serpula vermicularis (serpulid) | tek, kıvrık kireç borusu, halkalı | çap "rarely exceeds 5 mm"; boy 3-10 cm, en çok 18 cm; büyüme ortalama 9 mm/ay | boru pembemsi-beyaz; taç kırmızı, beyaz, turuncu ya da sarı | gelgit bölgesinden 100 m'ye | [seawater.no](https://seawater.no/fauna/annelida/vermicularis.html), [MarLIN BIOTIC](https://biotic.marlin.ac.uk/data/species/4310), [MarLIN](https://www.marlin.ac.uk/species/detail/1546), [Wikipedia](https://en.wikipedia.org/wiki/Serpula_vermicularis) |
| Spirobranchus triqueter | sırtlı beyaz boru | ≈3 mm geniş, ≤25 mm boy; 1,5 mm/ay | beyaz | Akdeniz dahil | [Wikipedia](https://en.wikipedia.org/wiki/Spirobranchus_triqueter) |
| Hydroides norvegica | ince düz boru | ≈2 mm geniş, ≈50 mm boy | beyaz, tebeşirimsi | kirlenme (fouling) canlısı | [Wikipedia](https://en.wikipedia.org/wiki/Hydroides_norvegica) |
| Spirorbis spirorbis (sarmal) | düz sarmal | ≈5 mm çap | beyaz | çoğunlukla alg ve deniz çayırı üstünde, ≤30 m | [Wikipedia](https://en.wikipedia.org/wiki/Spirorbis_spirorbis) |
| Perforatus perforatus (balanus) | koni, dar ağızlı | çap ve boy ≤30 mm | soluk mor-kahve, dikey sırtlı plakalar | gölgeli çıkıntılar, mağara ağızları | [Wikipedia](https://en.wikipedia.org/wiki/Perforatus_perforatus) |
| Vermetus triquetrus (vermetid) | düzensiz kıvrık boru-kabuk | 30-50 mm boy, kesit ≤6 mm | yazmıyor | | [Wikipedia](https://en.wikipedia.org/wiki/Vermetus_triquetrus) |
| Chama gryphoides | yapışık çift kabuklu | 20-28 mm | yazmıyor | 10-200 m | [Wikipedia](https://en.wikipedia.org/wiki/Chama_gryphoides) |
| Spondylus gaederopus | yapışık iri çift kabuklu | yazmıyor | alt kabuk beyaz, üst kabuk mor | ≤50 m, kayalık | [Wikipedia](https://en.wikipedia.org/wiki/Spondylus_gaederopus) |
| Schizoporella errata (bryozoa) | çok katlı kabuk ya da dallı yapı | 1 cm kalın katmanlar yaygın; yaygı 40 cm'ye kadar, dallı yapı 20 cm'den yüksek | koyu tuğla kırmızısı, büyüme kenarı turuncu-kırmızı | sert zemin ve yapay yapılar | [Bishop Museum](https://data.bishopmuseum.org/HBS/invertguide/species/schizoporella_errata.htm), [ENEA](https://ambiente.sostenibilita.enea.it/en/node/4546) |
| Myriapora truncata (bryozoa, "yalancı mercan") | kesik uçlu silindir dallar | koloni ≤10 cm geniş, ≈15 cm boy | yoğun kırmızı-turuncu | loş yerler, 2-100 m | [atlantisgozo](https://atlantisgozo.com/coral-sea-plants/myriapora-truncata-false-coral), [reeflex](https://reeflex.net/tiere/3873_Myriapora_truncata.htm) |
| Mesophyllum alternans (kireçli kırmızı alg) | kabuk | ≤1,2 mm kalın | kırmızı ya da soluk pembe | Posidonia dibi, korallijen | [Dryades, Trieste Üniv.](https://dryades.units.it/Corallinales/key/schede/Mesophyllum_alternans_e.htm) |
| Lithophyllum stictaeforme (kireçli kırmızı alg) | kabuk ve üst üste binen yarım daire lameller | orta kısım ≤2 mm kalın | soluk pembe-mor | korallijen yapıcısı | [Dryades](https://dryades.units.it/Corallinales/key/schede/Lithophyllum_stictaeforme_e.htm) |
| Crambe crambe (sünger) | ince levha, pürüzlü, kabarık delikli | ince | turuncu, turuncu-kırmızı | iyi aydınlık sert zemin, 5-30 m | [Wikipedia](https://en.wikipedia.org/wiki/Crambe_crambe) |

Fotoğraflardan yerleşim kuralları (`BASVURU.md` §12'deki ölçümlerle):
- Yukarı bakan yüzlerde açık gri-bej, tanecikli tortu keçesi; yan ve alt yüzler ince yeşilimsi film: üst/yan ≈4 kat parlak (Tyre 30.07).
- Canlılar ağız halkasında ve kulp-boyun girintisinde yoğun (Albenga PICT0036, Panarea III); açık gövdede seyrek tek tüpler.
- Kumla temas eden ve kumun aşındırdığı yüzlerde kabuk az, sert kaya ve yükseltide çok (Hvar Z-229).
- Koyu sünger lekeleri büyük gövdede alanın ≈%8'i (Hosta); kabuk tek renk değil, birkaç cm'lik leke mozaiği (Pišćena, koyu oran %19).
- Eski kırıkların kenarı da kaplı (Albenga PICT0380); temiz kil rengi yalnız yeni kırıkta.
- Gömülme çizgisinde dar, ≈10 kat koyu temas gölgesi (Tyre 23.10); gömülü gövdeler çaplarının üst 1/3-1/2'si kadar dışarıda (Vinogradišće).
- Tüylü alg/hidroit örtüsü siluete saçak verir; en kalın örtüde kulplar yine okunur (Lučišće).

## 4 · Öneri (ayrıntı)

**Geometri:** döner yüzey, dört tip: Dressel 1B (DR105) ağırlıkta, Dressel 1A (DR104), Dressel 2-4 İtalyan (DR139), Lamboglia 2
(DR136). Dış ve iç poliline (mm) döndürülür; iç profil kırıklarda duvar kesitini doğru verir. Kulplar dış ve iç kulp yolları
arasındaki orta çizgi boyunca kesit süpürülerek (Dressel 1 ve Lamboglia oval, Dressel 2-4 çift çubuk). Tip içi çeşitleme için
Southampton aralıkları sınır: Dressel 1 boy 97-117 cm (aynı tipte ≈±%9), en 28-29 cm. Döner yüzeyde v = profil yay uzunluğu
(mm) doğrudan gerçek ölçekli; çevre yönünde u = açı × yerel yarıçap gerçek ölçeği korur ama u aralığı satırdan satıra değişir
(döşeme ve dikiş buna göre kurulmalı); öteki yol nesne uzayında üç düzlemli izdüşüm. İki yolda da tekstel dünya ölçüsünden
doğrudan hesaplanır.

Neden tarama değil (sayılarla): (1) girişsiz indirdiğim üç taramanın dokusu 1024²: 1 m'lik nesnede 2,4-2,7 mm, hedefin 12-13 katı;
(2) Sketchfab özgünleri 4K'da bile ≈0,6 mm (tahmin) ve indirmesi hesap istiyor; 8K'lık iki aday (9 ve 10) Dressel 1B değil;
(3) üç indirilenin üçü de ölçeksiz; (4) ışık ve kabuk dokuya pişmiş, 40-80 kopyada aynı kabuk tekrar eder ve kırma zor; (5)
ArchAIDE profilleri mm, çizimle 1-2 mm içinde, Southampton aralıklarında ya da en çok ≈1 cm dışında, 1B boyu gerçek bir 1B ile
aynı, 2-4 hacmi yayımlanan aralıkta.

**Malzeme:**

| Katman | Set | Tekstel | Not |
|---|---|---|---|
| Gövde kili (kırık kenar, açık kalan yüz, mikro normal ve pürüzlülük) | Poly Haven `clay_plaster` | 16K: 0,122 mm; 8K: 0,244 mm | Planın "amfora 8K" bütçesinde 0,2 mm'yi %22 aşar: ya normal ve yükseklik 16K, albedo 8K; ya da 8K'yı 1,64 m'ye sıkıştırıp (özellik boyu %18 küçülür) sapmayı DURUM'a yaz. Albedo kendi rengimizle boyanmalı (okra → pembe-turuncu; flaşlı Vis karesinde gövde sRGB(190,145,108)). Southampton: Dressel 1 hamuru "Campanian 'Black Sand' fabric" |
| Kabuk ve konkresyon | Poly Haven `coral_fort_wall_01` | 8K: 0,166 mm ✓ | Maske altında; tüylü örtü ve tüp, balanus, bryozoa kodla. Kemer kaplamasıyla aynı set: tek yükleme, iki kullanım |
| Kemer (yakın) | ambientCG `Rock043S` ya da depodaki `rock_boulder_dry` 16K | 0,110 mm ✓ (ikisi de) | `rock_boulder_dry` 16K Ultra'da zaten bellekte: ek bellek yok. Gri karst görünüşü istenirse Rock043S 8K (0,90 m tekrarı uzakta görünür: Rock043L ile karıştır) |
| Gövde kili, görünüş adayı | ambientCG `Clay002` | bilinmiyor | Çömleğe en benzeyen; fiziksel boyutu yazmıyor: indirip bilinen bir özellikten ölçmeden seçilmez |

Bellek (hesap): BC7/BC5 bir harita texel başına 1 bayt; 8K harita mip'lerle ≈85 MiB, 16K ≈341 MiB. Üç haritalı (albedo, normal,
pürüzlülük) 8K set ≈256 MiB, 16K set ≈1 GiB.

**Kabuk katmanı:** kodla; §3.3'teki ölçü ve renk tablosu ve yerleşim kuralları. Renk değerleri yazılı tanım (yansıtma ölçümü
değil); flaşlı kareler (Albenga PICT0036, Vis Z-5101) albedo yönü için en yakın veri.

**LICENSES.md için önerilen satırlar** (bu ajan dosyaya yazmadı; CC BY içerik için `cc0/` adı yanıltıcı olur):

| Klasör (öneri) | Kaynak | Lisans |
|---|---|---|
| `ccby/amfora_profil` | ArchAIDE consortium (2019), Southampton çizimlerinin vektör profilleri (Dressel_1_DR105, DR104, Dressel_2-4_Italian_DR139, Lamboglia_2_DR136), https://doi.org/10.5284/1050896 ; çizimler Penny Copeland, University of Southampton (2014) https://doi.org/10.5284/1028192 | CC BY 4.0 (atıf zorunlu) |
| `cc0/amfora_kil` | Poly Haven, clay_plaster (https://polyhaven.com/a/clay_plaster) | CC0 1.0 |
| `cc0/kabuk` | Poly Haven, coral_fort_wall_01 (https://polyhaven.com/a/coral_fort_wall_01) | CC0 1.0 |
| `cc0/kemer` | ambientCG, Rock043S (https://ambientcg.com/view?id=Rock043S) | CC0 1.0 |

## 5 · Doğrulanmadı

- ambientCG Clay001-003 ve Rock017-019, Rock026'nın fiziksel boyutu (API'de 0); ambientCG zip'lerinin harita listesi (API'de boş döndü).
- ambientCG `dimensionX/Y` biriminin cm olduğu: API belgesinde yazmıyor, Ground054 tutarlılığından çıkarım.
- Sketchfab özgün dosyalarının doku çözünürlüğü: görüntüleyicinin listesinden okundu, indirme paketiyle aynı olduğu doğrulanmadı;
  8K'lık adayların UV kullanımı ve gerçek tekstel'i ölçülemedi (indirme hesap istiyor). "4K'da ≈0,6 mm" bir tahmindir.
- Objaverse'te dokunun 1024²'ye inmesinin genel olduğu: yalnız indirdiğim üç dosyada gördüm.
- 9 numaralı Lefkada modelinin "Dressel 2-4" iddiası (önizleme uymuyor); Amph 941/951'in tipi (küresel gövde, kaynak yazmıyor);
  Ben Kreimer modelinin birimi ve gerçek boyu.
- Profiller tek bir çizilmiş örnek; tip içi değişkenlik ortalanmadı. Dressel 1 duvar kalınlıkları (1B 23-25 mm) çizimden, gerçek
  örneklerle kıyaslanmadı. Kulp kesitinin hangi kenarının radyal olduğu çıkarım. Hacimler ağıza kadar, ölçüm tanımı kaynaklarla
  aynı mı bilinmiyor.
- ADS'nin ArchAIDE OBJ metadata'sındaki köşe sayısı (496.465) ile dosyadaki (126.081) farkın nedeni.
- Southampton ve ArchAIDE lisans metinleri canlı siteden değil Wayback kopyalarından okundu (canlı site Cloudflare doğrulaması istedi).
- Tarih uyumu (dört tipin aynı yükte olabileceği) Southampton tarih aralıklarından çıkarım; gerçek bir yükte bu dördünün birlikte
  bulunduğuna kaynak aramadım.
- Kabuk canlılarının ölçüleri genel tür sayfalarından; amfora üstünde ölçülmüş değerler değil. Renkler sözel, yansıtma ölçümü yok.
- `BASVURU.md` §12-13'teki tür adları yalnız sayfada yazdığı kadar; flaş var/yok etiketleri görsel yargı; Albenga'nın 42 m
  derinliği Commons'ta değil, scubaportal'da yazıyor.
