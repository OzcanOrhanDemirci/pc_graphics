# DURUM · Sahne 9 "Ayna"

> **Özcan'ın sabah bakacağı yer bu bölümdür.** Otonom gece oturumu, 2026-10-07 00:40 → (sonu en altta). Plan
> `docs/ayna/PLAN.md`; plandan sapmalar aşağıda "Kararlar"da, gerekçesiyle.

## Sabah: nasıl açılır, ne denenir

- `build\pc_graphics.exe` → galeride **09 Ayna** → Enter. (Isı taslağı silindi; yerinde bu sahne.)
- Fare tıklamadan bakar (Denizaltı'nın sözleşmesi), **W A S D** yatay, **Boşluk / C** yüksel-alçal, **Shift** hızlı,
  **Tab** imleci bırakır. Duvarlardan, sütunlardan, aynadan geçilmez.
- Işığımız: **Q / E** renk (sıcak beyaz, gün ışığı, kırmızı, turuncu, yeşil, camgöbeği, mavi, mor), **tekerlek** şiddet
  (8 kademe), **F** aç / kapa. **G** ya da **sol tık** önüne lamba bırakır (o anki renk ve şiddette, en çok 8), **X**
  hepsini toplar, **R** başa döner, **1-4** bestelenmiş bakışlar (koridor, ayna, salon, zemin). **↑ ↓** pozlama. Ayar paneli
  yok (karar 6): kalite 2560 × 1440 için ayarlı.
- Denenecekler: (1) iki uzun koridordan birinde **aynaya dik bak**: tünelin kopyaları, kürenin kendisi; (2) ışığı
  bir aynaya **yatık** tut: yansıyan koni duvara ve pusa düşer; (3) farklı renkte iki üç lamba bırakıp uzaklaş:
  hepsi her yöne sonsuza çoğalır; (4) bir kapı aralığından geçerken ışığın pervaza düşüşü (yakından keskin).

## Bir bakışta (son hâl)

- **Labirent:** 60° üçgen ızgarada altıgen (54 hücre, 1,6 m kenar), 44 ayna (18'i dış duvar), 46 geçit; ortada altı
  hücrelik salon, **iki uzun düz koridor** (duvarları paralel ayna: "yansımanın yansımasının yansıması"). Her kenar
  aynı modül: sütunlar köşelerde (r 15 cm), duvar 9 cm, at nalı kemerli pencere (1,2 m, kemer yarıçapı 0,64 m, tepe
  2,69 m), tavan 3,4 m. Pencere ya geçittir ya da içinde iki yüzlü ayna vardır: aynalar ızgara çizgilerinde durduğu için
  yansıma ızgarayı kendine taşır, ayna ile geçit ayırt edilmez (Luzern 1896'nın oyunu).
- **Görünüş:** Kurtuba Camii ve Elhamra'dan: kırmızı tuğla / krem taş kemer taşları, altın alfiz, kabartmalı alçı
  köşelikler, mavi zeminde altın yıldızlı friz, yıldız kafesli koyu sedir tavan, yeşil damarlı mermer sütunlar ve altın
  başlıklar, ceviz ayna çerçeveleri, siyah damarlı cilalı mermer zemin, zincirli dört pirinç fener.
- **Biz:** 35 cm'lik krom küre; göz merkezde, kendimizi yalnız aynalarda görürüz; ışık önümüzdeki yarıktan.
- **Hız:** 1440p, 6 sabit örnek: kare 21-28 ms + ışığımızın pişirmesi 1,8 ms. Canlı uygulamada (otomatik test, dört
  bakış ve yürüyüş) **29,5-44 FPS**; en ağırı aynaya yaklaşıp krom küremizin ekranı doldurduğu an (~29,5 FPS).

## Fizik ne derse o: değerler ve kaynakları

| | Değer (610 / 550 / 465 nm) | Kaynak |
| --- | --- | --- |
| Float cam, kırılma | 1,5225 / 1,5251 / 1,5307 | M. Rubin, *Sol. Energy Mater.* 12, 275 (1985), "clear" (refractiveindex.info) |
| Float cam, soğurma 4πk/λ | 10,7 / 5,0 / 4,6 m⁻¹ | aynı, tablodaki k |
| Gümüş (camın arkasında) | n 0,058 / 0,060 / 0,047, k 4,09 / 3,60 / 2,80 | P. B. Johnson, R. W. Christy, *Phys. Rev. B* 6, 4370 (1972) |
| Krom (küremiz) | n 3,19 / 2,79 / 2,14, k 4,27 / 4,20 / 3,93 | A. D. Rakić vd., *Appl. Opt.* 37, 5271 (1998) |
| Cilalı mermer | n 1,57 | kalsitin iki kırılma indisinin ortası (1,486 / 1,658) |

- **Ayna** (6 mm cam + gümüş + 6 mm cam): ön yüzde Fresnel, kırılıp gümüşe, geri, camdan çıkış; iki yöndeki soğurma.
  Dik gelişte yansıtma **0,79 / 0,84 / 0,84**: kırmızı daha çok kaybolur, tünelin derini **camgöbeği-yeşile döner**
  (10 sekmede 0,10 / 0,18 / 0,18). Işın camın içinden yana kayar (2 g tan θ_t, yatıkta ~1 cm): görüntü birkaç mm
  oynar, bu da fizik. Ayarı yok, açıya göre tablo (128 nokta).
- **Krom küre:** dik gelişte 0,64 / 0,65 / 0,66, yatıkta artar.
- **Zemin ve sütunlar:** cilalı taş, Fresnel ile yansır (dikte %5), kalanı dağınık.
- **Sekmenin kesilmesi:** yolun ağırlığı 1/1024'ün altına inince (tünelde ~38 sekme; 1/4096 ile fark piksellerin %0,007'si).
- **Işıklar:** ışığımız 25-3200 cd (8 kademe), koni 28° / 12°, yarık 3 cm; lamba aynı ışık akısını her yöne verir
  (tepe şiddetinin %3'ü), 3 cm cam küre; fenerler 6 cd, ~1900 K, 7 cm delikli kafes.
- **Pus:** σ_s 0,006 /m, σ_a 0,001 /m, ileri saçan (Henyey-Greenstein g 0,6: sis makinesinin damlacıkları, toz); huzmeyi
  gösterir, lambalara bakınca hâle yapar, 50 m'lik derin tünelde yansımaları %30 söndürür.

## Nasıl çiziliyor (mimari)

- **Analitik ızgara izleyici (CUDA), OptiX değil** (plandan sapma, gerekçe "Kararlar"da). Işın hücreden hücreye yürür;
  her hücrede yalnız oranın zemini, tavanı, üç sütunu, üç duvarı (yüz, pervaz, kemer içi, aynanın camı ya da geçidin
  orta düzlemi) ve oradaki nesnelerle kesişir. Yüzeyler kesin (düzlem, çember, silindir): yürütme yok, sınır hacmi yok,
  "kendi kendini kesme" ötelemesi yok (az önce çıkılan yüzeyle yapı gereği yeniden kesişilmez).
- **Aynadan duvara ışık: hüzme ağacı.** Yol izleme nokta ışığın aynadan duvara yolunu bulamaz. Aynalar dik ve ızgara
  çizgilerinde olduğu için ışığın yolu planda kesin: her ışıktan bir kama, pencerelerden geçerken sütunların hücre
  içindeki 60°'lik dilimleri ve pervazlar ile kırpılır, aynada ışığın görüntüsü yansır (Heckbert-Hanrahan 1984; akustiğin
  görüntü kaynakları). Bir yüzey noktası, hücresindeki kamaların kendisini gördüğü her görüntüden ışık alır:
  I · W / d², W aynaların o yolun kendi açılarındaki ölçülmüş yansıtması. Kemer bölgesinde (jamb tepesinin üstü) yolun
  her pencereden kemerin altından geçtiği 3B'de denetlenir. Örnekleme yok: gren yok.
- **Işık haritaları.** Lambalar ve fenerler durur: ışıkları (doğrudan ve bütün aynalardan) bırakıldıkları an aynı hüzme
  hesabıyla yüzeylere pişirilir (23 M texel: zemin, tavan, duvar yüzleri 1 cm; pervaz ve kemer içleri 2,5 mm; sütunlar
  5 mm; ışık başına ~10 ms). Texel doğrudan gördüğü lambaları bitle tutar: küremizin lambalardan düşen gölgesi piksel
  başına analitik (iki diskin örtüşmesi). Işığımız bizimle yürüdüğü için **her kare** yarım çözünürlüklü ikinci bir
  haritaya pişer (1,8 ms); kendi hücresindeki doğrudan ışığı ise piksel başına kesin (yakındaki pervazda pencere
  kenarının keskin gölgesi texele yuvarlanmasın).
- **Küçük ışıklar koniyle.** Her ışın kendi izdüşüm konisini taşır (düz aynada değişmez, kürede ve sütunda açılır).
  Lambalar ve uzaktaki fenerler konideki örtüşmeleriyle (disk-disk) toplanır: piksel altına inen derin tünel lambaları
  örneklerin birine çarpıp ötekini kaçırarak titremez, sabit bir nokta olur.
- **Desenler** (kemer taşları, alçı, friz, tavan, mermer damarları) ışının izdüşümüyle süzülür: aynanın ardındaki desen,
  aynı uzaklıktan doğrudan bakıldığı kadar keskin ve sakin.
- **Pus**, her yol parçası boyunca kapalı biçimde (nokta ışığın saçılması açıya çevrilip 4 Gauss noktasında faz, koni ve
  aynalar): ışığımızın bütün hüzmeleri (aynadan yansıyan koni pusta görünür) ve lambaların doğrudan hüzmeleri; yarım
  çözünürlükte, derinliğe duyarlı büyütmeyle.
- **Örnekleme:** pikselde 6 sabit örnek (6 kale deseni), her kare aynı, komşu piksellere hafif taşan yeniden
  yapılandırma; birikim ve gürültü giderici yok. Çıktı OpenGL dokusuna kartın içinde kopyalanır.

## Ölçümler (Ö1-Ö6)

| # | Ölçüm | Hedef | Sonuç |
| --- | --- | --- | --- |
| Ö1 | Netlik: aynı görüntü doğrudan ↔ gözün 5 cm önündeki analitik bir aynadan (göz ve örnek deseni aynalanmış) | piksel piksel aynı | **İdeal ayna: aynı** (8 bitte ortalama 0,47/255 fark: titreşim deseni aynalanmıyor; keskinlik ölçüsü 0,1756 ↔ 0,1756). Labirentin aynası (cam + gümüş): W kadar kararır, cam görüntüyü birkaç piksel kaydırır, **bulanıklık yok** (keskinlik 0,1846). `--test o1` |
| Ö2 | İki paralel aynada tünelin n'inci kopyası analitik yerinde | < 0,1 px, n ≤ 40 | **≤ 0,0001 px** (n = 40'ta yol 55 m; analitik hesap camın n kez kırılmasıyla). `--test o2` |
| Ö3 | Kararlılık, `tools/kararlilik.py` | Ada/Denizaltı (~0,001) | **Tutmadı (mutlak), yaklaştı:** piksel kıpırtısı 0,0014 (16 örnek referansa), 0,0020 (64 örnek HDR referansa); blok 0,0005 / 0,0007. Gece boyunca 0,0046'dan indi: küçük ışıklar koniyle, ileri saçan pus, sade çerçeveler, 6 örnek, komşu piksellere hafif taşan yeniden yapılandırma (`recon` 0,12: keskinlik −%10). Koy'un yakın yolunun (0,0016) altında, yakınsamış referansla Ada'nın ~iki katı. Harekete göre bağıl 0,049 / 0,072. Kalan kaynak: sütun ve çerçevelerin dikey kenarları, derin tünelde piksel altına inen kemerler. **Ölçünün tabanı:** 16 örneklik canlı kare bile 64'lük referansa karşı 0,00156 veriyor; 6 örnek 16'lık referansa karşı aşağı yukarı 16 örneğin 64'e karşı durduğu yerde. `--test o3` |
| Ö4 | Aynadan ışık: hüzme haritası ↔ aynı izleyiciyle foton (1 milyar, Fibonacci yönleri, aynada cam ve gümüş, yan kayma dahil) | ≥ 0,99 | **0,994** (hücre ortasındaki lamba; doğrudan 0,997, aynalardan 0,984), **0,990** (kapı aralığındaki lamba; doğrudan 0,995, aynalardan 0,978). Aynalardan gelen ışığın payı %22-28. Kalan %2: hüzmeler camın yan kaymasını ve şişkin kemer bölgesini saymaz. `--test o4` |
| Ö5 | 1440p, büyütmesiz | 30-40 FPS, en kötü bakışta da | Başsız: kare 21-28 ms + 1,8 ms. **Canlı uygulamada 29,5-44 FPS**; en kötü bakış (krom küremiz aynada ekranı doldururken) ~29,5: hedefin sınırında. Çıktı CUDA-GL ortak dokuya gider (ana bellek gidiş-dönüşü yok). |
| Ö6 | Bozuk piksel (NaN), sekme noktası lekesi | 0 | **0** (bütün çizimlerde sayaç 0; 1000 rastgele pozda CUDA temiz, `--test fuzz`). |

Ö3 mutlak hedefi tutmadığı (Ö5 de en kötü bakışta sınırda) için plandaki kapıya uyuldu: **A7 (kırılma) başlatılmadı.**
Nasıl yapılabileceği aşağıda "Sıradaki"de.

## Kararlar ve plandan sapmalar

1. **OptiX yerine analitik ızgara izleyici.** Aynalar düşey ve ızgara çizgilerinde: ışın hücreden hücreye kesin
   yürüyebiliyor. Kazanç: kesinlik (Ö2 0,0001 px), öteleme yok, BVH yok, aynı temsil hüzme ağacını da besliyor; hız
   (1440p'de salt izleme 4 örnekle ~6-13 ms).
2. **Işık haritaları** (planda yoktu). Piksel başına hüzme değerlendirmesi lamba başına ~9 ms tutuyordu (her yöne yayılan
   lambanın yüzlerce görüntüsü var); 8 lambayla bütçe patlıyordu. Lambalar durduğu için pişirildi; ışığımız da her kare
   pişiyor, kendi hücresindeki doğrudan ışığı piksel başına.
3. **6 örnek + hafif geniş yeniden yapılandırma.** 4 örnek kıpırtı 0,0029, 8 örnek 0,0024 ama en kötü bakışta 40 ms; 6 kale
   deseni 0,0024 ve 31 ms. Pikselin örneklerinin komşulara hafifçe taşması (`recon` 0,12, Blackman-Harris'in kuyruğu gibi)
   kıpırtıyı %30 azaltır, keskinliği %10; 0,18 daha sakin ama ince filetolar yumuşuyordu.
   Uyarlamalı ek örnek denendi: iraksama yüzünden 8 sabitten yavaş (40-61 ms), bırakıldı.
4. **Hayalet görüntü** (camın ön yüzünün yansıması) yazıldı, ölçüldü: görünür etkisi piksellerin %0,001'i, maliyeti
   +%40. Kapalı (`ghost.mirrors = 1` açar).
5. **At nalının şişkinliği** (jamb tepesinin üstünde pencerenin kenara doğru genişleyen kısmı) hüzmelere verilmedi
   (`beam.bulge = 1` açar): düğüm sayısını 2,4 katına çıkarıp farkı %5 azaltıyordu. Oradan geçen biraz ışık eksik.
6. **Ceviz çerçeveler** ilk denemedeki krem pervazların ve altın filetoların yerini aldı: görüntü "şeker çubuğu" gibi
   çizgiliydi, ince parlak çizgiler kıpırtıyı da artırıyordu.
7. **İleri saçan pus.** İzotrop pusta ışığımız (göze 17 cm uzakta) her bakışın ortasına parlak bir disk yapıyordu (siste
   uzun far); gerçek pus ileri saçar, geri saçılma 6 kat azaldı.
8. **Işığımızın doğrudan ışığı piksel başına.** Haritada (1 cm) yakındaki pervaza düşen pencere kenarı gölgesi basamak
   yapıyordu; gölge ışınlı kesin yöntemle kıyaslanıp düzeltildi (üç yöntem artık aynı görüntüyü veriyor).
9. **Kenar iyileştirmesi denendi, geri alındı (sabaha karşı).** Canlı 6 örnek 16'lık desenin alt kümesi yapıldı; yerel
   karşıtlığı yüksek piksellere (%3-6) kalan 10 örnek ayrı, sıkıştırılmış bir geçişte veriliyordu. 16'lık referansa karşı
   kıpırtı 0,0014 → 0,0007 ölçüldü, ama bu büyük ölçüde kenar piksellerinin referansın kendi desenini birebir kopyalaması:
   4 ek örnekle hiç kazanç yok (0,00137), 64'lük referansa karşı kazanç yalnız %11 (0,0020 → 0,00177). Bedeli kare başına
   6-8 ms (en kötü bakış 26 → 34 ms). Ö5'i bozup ölçüyü "kandıran" bir kazanç olduğu için alınmadı.

## Gece boyunca bulunan ve düzeltilen hatalar (kısa)

- Zemine çarpan ışının y'si −1e‑7 olunca pencere testi düşüyordu (zeminde ızgara çizgileri) → zemine oturtma.
- Aynadan çıkış noktası camın kaymasıyla zeminin altına iniyordu (ayna-zemin birleşiminde parlak çizgi).
- Yüzdeki noktalar "duvarın içinde mi" testinde titriyordu (tuz-biber benekler).
- Pervazlara ışık, duvar kalınlığının içinden, öbür taraftan da girer: komşu hücrenin (geçit) ya da noktanın aynadaki
  görüntüsünün (ayna) düğümleriyle değerlendirme eklendi, çift sayım olmadan.
- Işık bir kapı aralığının içindeyken kapının zemini karanlık kalıyordu (Ö4'ün fark haritası buldu).
- Canlı uygulamada bir bellek hatası: rastgele 2000 poz taramasıyla yeniden üretildi, düğüm dizinlerine sınır denetimi
  eklenince kayboldu, hiçbir ihlal sayılmadı (aşağıda "doğrulanmadı").

## NE DOĞRULANMADI

- **Canlı kontroller bir insan elinde denenmedi**: fare ve A/D yönü sözleşmeye göre yazıldı (görüntünün sağı = render'ın
  sağ vektörü), otomatik test sanal tuşlarla geçti; ters kontrol iki kez kaçmıştı, ilk denemede bak.
- Çarpışmanın hissi (kayma, kapıdan geçiş) denenmedi; küre çarpışma çözümü sayısal gradyanla.
- O bellek hatası **kaynağı bulunmadan** kayboldu (sınır denetimleri derlemeyi değiştirdi). 1000 + 2000 rastgele poz
  ve otomatik test temiz; yine de bir gün dönebilir. **Aynı sınıftan bir iz** (karar 9'un denemesinde): çok dağınık
  warp'larla (her warp'ta ekranın farklı yerlerinden pikseller) çalışan çekirdek, aynı girdiyle süreçten sürece birkaç
  pikselde farklı sonuç verdi ve arada "Out Of Range Address" ile düştü (sürücü günlüğü: SM warp exception). Aynı
  pikseller komşularıyla aynı warp'ta işlenince her seferinde aynı; `-Xptxas -O0` ile derlenince aynı; kaynak kodda
  piksel başına davranış başka şeritlere bağlı değil. Derleyicinin (CUDA 13.4 ptxas) dağınık warp'ta yanlış kod üretmesi
  güçlü aday, kanıtlanmadı. Deneme geri alındı; asıl çekirdekte dört bakış 4 ayrı süreçte bit bit aynı, 3000 pozluk
  1440p taraması temiz.
- Ö3'ün mutlak hedefi tutmadı (yukarıda). Görsel olarak derin tünelin kaçış noktasında ince kıpırtı olabilir.
- Pusu yalnız ışığımız (bütün hüzmeleri) ve lambaların doğrudan ışığı aydınlatır; lambaların aynalardan gelen ışığı
  pusa katılmadı.
- Duvarlardan dağınık sekme (ışığın alçıya çarpıp zemine düşmesi) ve cilalı zeminin yansıttığı ışığın duvara düşmesi
  yok: yalnız aynalar.
- Küremizin gölgesi: lambalarda yalnız doğrudan ışıkta; ışığımızın aynadan dönen ışığında yalnız son parçada.
- Fenerin delikli kafesinin ışığı yıldız deseninde değil, düzgün yayılır.
- Hüzmeler camın yan kaymasını saymaz (görüş ışınları sayar): aynadan düşen ışık birkaç mm kayık olabilir.
- Ses, toz, çatlama (A7) yok.

## Araçlar

```bat
build\pc_graphics.exe --scene ayna --headless --out captures\ayna [--view 0..3] [--cam "x y z yaw pitch fov"] [--lamp "x y z renk seviye"] [--light "renk seviye açık"] [--plan] [--probe "x y"]
build\pc_graphics.exe --scene ayna --test o1|o2|o3|o4|fuzz|lambalar
python tools\kararlilik.py captures\ayna\o3
```

Kod: `src/scenes/ayna/` (`ayna_geom.h` ızgara, `ayna_maze.*` labirent ve çarpışma, `ayna_beams.*` hüzme ağacı,
`ayna_kernels.*` izleyici, ışık haritaları, pus, `ayna_material.h` görünüş, `ayna_app.*` durum ve araçlar,
`ayna_live.cpp` sahne). Ayarlar `scenes/ayna.cfg`.

## Sıradaki (Özcan isterse)

- **A7 kırılma:** çatlama yalnız görüntü olarak ucuz: darbe noktasından Voronoi parçaları (radyal + halka), her parçanın
  normali 0,5-1,5° eğik, çatlak çizgisinde camın yeşil kenarı; ışın aynaya çarptığında parçayı bulup kendi normaliyle
  yansıtır. Hüzmeler çatlak aynayı düz sayar (ışık birkaç mm kayar). Dökülme (7-8 parça yere) Yıkım'ın katı cisim
  çözücüsünü ister: parçalar hücrelere nesne olarak girer.
- Kıpırtı için tek büyük kaldıraç kalan: örnek sayısı (8 örnek en ağır bakışta 40 ms; çekirdek hızlanırsa). Ölçünün
  tabanı yüksek (16 örnek bile 64'e karşı 0,00156); ~0,001 için ya 16+ örnek ya da zamansal birikim gerekir, birikim ise
  aynadaki görüntüde hareket vektörü olmadığından iz bırakır.
