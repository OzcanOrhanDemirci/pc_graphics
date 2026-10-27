# DURUM · Sahne 11 "Orman"

> Plan `docs/orman/PLAN.md`; varlıkların kaynağı ve lisansı `docs/orman/VARLIK.md`. Özcan'la etkileşimli ilerleniyor
> (2026-10-27: *"Sahnede ilerledikçe [...] 11. sahneyi açıp fikirlerimi belirtebilirim"*).

## Özcan: nasıl açılır, ne denenir

- Varlıklar git'te değil: önce `python assets/orman/fetch.py` (indirir, her dosyayı özetiyle denetler; 50 varlık, diskte ~1,2 GB + zipler 0,4 GB).
- `build\pc_graphics.exe` → galeride **0**, sonra **aşağı ok** (11. sahnenin sayı tuşu yok) → Enter. İlk yükleme
  ~13 sn (taramalar 7 sn, ışık 4 sn).
- Fare tıklamadan bakar (Tab imleci bırakır), **W A S D** yürür, **Shift** koşar, **Boşluk** zıplar, **C** eğilir.
  **1 2 3 4** bakışlar (dere boyunca, devrik gövde, yukarı yapraklara, yer), **R** başa, **↑ ↓** pozlama. Panelde
  **Örnek 1 / 4 / 8 / 16** (pikselde sabit desenli ışın; varsayılan 4).

## Şu an (2026-10-27): ilk iskelet, taşlar O0-O2'nin ilk hâli

Plandaki taşlar sırayla değil, ilk bakışı en çok değiştirenden başlayarak yürüdü:

- **Varlık hattı (O0):** `assets/orman/fetch.py` + `manifest.json` (Poly Haven ve ambientCG, her dosya özetiyle);
  glTF'i doğrudan okuyan yükleyici (cgltf, MIT): her düğüm (varyant) bir prototip, her primitif onun GAS'ında bir
  yapı girdisi; Poly Haven'ın JPG temel rengi alfasız geldiği için alfa haritası yanındaki PNG'den, adından eşlenir.
  Alfa haritasının mip zinciri kapsamayı korur (Castaño): uzakta yaprak incelmez. Albedo mip'leri doğrusal ışıkta.
  Kaba LOD düğümleri (`_LOD1+`) atlanır. Varlıkların seçimi ayrı bir ajanın işi, sürüyor.
- **Görüntü ışın izlemeyle (O1, §3.1):** OptiX 9.1, piksel başına sabit desenli N görünürlük ışını; örnekler
  yeniden yapılandırma süzgecinin (Gauss, σ 0,42 px) kendi dağılımından çekilir, hepsi eşit ağırlıkta (komşu piksel
  belleği yok, her kare aynı). Yaprak kenarı any-hit ile alfa testi (üç yolun kıyası daha yapılmadı). Doku seviyesi
  ışın konisinden (Akenine-Möller 2019), üçgenin kendi doku yoğunluğuyla.
- **Pişmiş ışık (O4'ün ilk hâli):** 30 cm'lik yoklama ızgarası, her yoklama L1 SH (192 sabit yön, 2 geçiş: gök +
  güneş, sonra bir sekme), içeride kalan yoklama arka yüz payıyla geçersiz sayılır; okuma 8 yoklama, geçerlilik ve
  yüzeyin önünde olma ağırlığıyla. Zeminin kendi kapanması köşe başına (bitkilerin altı, 0,6 m, 48 ışın). Yaprağın
  arkasından gelen ışık aynı ızgaradan ters normalle.
- **Güneş canlı:** her örnek güneş diskinde kendi sabit noktasına bir gölge ışını (plan güneşi ızgarada pişirmeyi
  söylüyordu, aşağıda "Kararlar").
- **Pus ve huzmeler:** yerde yoğun, yukarı incelen pus; güneşin havadaki görünürlüğü 10 cm'lik bir hacimde pişmiş
  (voksel başına 4 gölge ışını), Henyey-Greenstein g 0,7. Adımlar göze yakın sıklaşır, pikselin örnekleri adımların
  içinde ayrı yerlere düşer.
- **Dere (O2'nin ilk hâli):** yatak zeminde oyulmuş, su yüzeyi yatak boyunca bir şerit; akışla kayan dalgacıklar,
  Fresnel payıyla yansıma ışını, Snell penceresinden kırılma ışını ve çay rengi soğurma, güneşin pırıltısı. Su gölge
  yapmaz (görünürlük maskesi).
- **Islaklık:** pürüzlü yüzeyler parlar (`wetness`), parlaklık ızgaranın ayna yönündeki ışığından.
- **Yerleşim (O3'ün kaba hâli):** sebepli kurallar: devrik gövde derenin üstünde kıyılara dayanır, kayalar kıyıda yarı
  gömülü, taşlar yatakta, ağaçlar ortadaki aralığın dışında, eğreltiler nemli kıyıda yoğun, geniş yapraklılar
  öbekler hâlinde, yosun yatağın ıslak kenarında halı. Gövdeler ve kayalar yürüyüşte engel.
- **Kenar gizlendi (vadi):** 44 m'lik ayrıntılı zeminin çevresinde 120 m'lik, 25 cm aralıklı ikinci bir zemin; 44 m'nin
  dışında yamaçlar dengesiz bir eğimle 28 m boyunca 9 m'ye kalkar (dere yatağının üstünde kalkmaz), dere parçanın dışında
  iki ucunda ters yönlere kıvrılıp gözden çıkar (su şeridi de onunla uzar). Parça birebir aynı kaldı: halkanın kopyaları
  parçadan sonra yerleşir. Halkada 20 dev ağaç (parçayla güneş arasındaki 35°'lik dilimde yok: ışığın geldiği aralık),
  yamaçlarda genç ağaçlar, yürünen alanın kenarından (15 m) dışarı göz hizasında bir çalı duvarı (90 cm'yi geçen alçak
  bitkiler, biraz büyütülmüş: bugün yalnız pachira). Izgaranın dışındaki yüzey ızgaranın en yakın yüzünün ışığını alır.

### Ölçümler (2026-10-27, RTX 4080)

| | |
|---|---|
| Varlık / prototip / malzeme | 23 / 80 / 30 (o anki indirme) |
| Kopya | 55.228; kopyalanmış üçgen ~400-418 milyon |
| Bellek | doku 2,0 GB, GAS 251 MB, geometri 341 MB, ızgara 121 MB, güneş hacmi 51 MB; uygulamada kart 7,8 / 16 GB |
| Yükleme | taramalar 7,4 sn, dünya 0,5 sn, yapılar 0,9 sn, zemin AO 16 ms, ızgara 4,1 sn, güneş hacmi 0,1 sn |
| 1440p, bakış 0, `--bench 30` | 1 örnek 5,8 ms · **4 örnek 24,2 ms** · 8 örnek 59 ms · 16 örnek 116 ms; pussuz 4 örnek 21,4 ms |
| Uygulamada (`--autotest-only orman`) | 46,7 FPS, kare 21,3 ms |
| Vadi ve halka (aynı oturumda `world.outer=44` ile kıyas, 1440p, 4 örnek, `--bench 30`) | bakış 0: 26,6 → 27,9 ms · bakış 1: 17,9 → 19,8 ms; kopya 55.228 → 64.457; yükleme ~+1,3 sn (ızgara 3,3 → 4,4 sn) |

### Kararlar (plandan sapmalar)

- **Güneş ışınla, ızgarayla değil:** plan güneşin görünürlüğünü 3B ızgarada pişirmeyi söylüyordu (ışın yok). Gölge
  ışını yaprak alfasıyla birlikte örnek başına bir ışın: yakın yaprağın keskin gölgesi ızgaranın çözemeyeceği kadar
  ince (30 cm'deki yaprağın yarı gölgesi ~3 mm), maliyeti ölçüde küçük. Izgara yalnız pusun huzmeleri için.
- **Su erken geldi:** plandaki gibi ışınla, ama O2'yi beklemeden, ilk bakış için.
- **Sonlu olmayan örnek sayılır ve atılır:** taramalarda sıfır alanlı üçgenler var; birinin NaN'ı ışıltı
  piramidinde siyah bir kare olarak yayılıyordu. Normal sıfırsa geometriden, o da sıfırsa ışından alınır.

## Açık işler (sıra Özcan'ın bakışına göre değişir)

1. **Tropikal varlıklar:** Poly Haven'da yağmur ormanı ağacı, palmiye, sarmaşık, monstera, muz, eğrelti ağacı yok;
   bugünkü taç jakaranda ve küçük ağaçlarla "orman" okunuyor, "yağmur ormanı" değil. Sketchfab (CC-BY) için Özcan'ın
   anahtarı gerekiyor (PLAN §6).
2. **Güneş beneği greni:** güneşin yaprak arasından süzüldüğü yerlerde tek piksellik parlak benekler var (bakış 1'de
   gövde ve zemin, vadiye bakınca kıyılar). Kenar işinden önce de vardı, halka ağaçları olmadan da çıkıyor. Olası
   kaynak (sınanmadı): örnek başına tek gölge ışını; uzaktaki ince yaprakçığın yarı gölgesi pikselden geniş, dört ışın
   onu ikili açıp kapatıyor. Netlik kuralına aykırı; denenecek yol: gölge ışınlarını çoğaltmak ya da uzak kapanmayı
   süzülmüş hacimden almak.
   (Dünyanın kenarı 2026-10-27'de vadiyle kapandı, yukarıda.)
3. Zemin karışımı (çamur, çakıl, yaprak katmanı), kompozisyon (devrik dev, payanda köklü ağaç), yaprak kenarının üç
   yolu, kör test, kıpırtı ölçümü, elde kamera kipi.

## NE DOĞRULANMADI

- Yürüyüş, çarpışma ve bakışlar elle canlı denenmedi; yalnız otomatik test (açılış, panel, yardım) ve komut satırı
  kareleri.
- Kıpırtı (`tools/kararlilik.py`) ölçülmedi; yaprak kenarı hareket ederken nasıl, bilinmiyor.
- Bakış 3'te tek bir sonlu olmayan örnek hâlâ var (atılıyor, görüntüde iz yok); kaynağı bulunmadı.
- Normal haritasının yeşil kanalının yönü (glTF'in v'si aşağı) mantıkla seçildi, ışık açısıyla sınanmadı.
