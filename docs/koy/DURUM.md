# DURUM · sahne 5 "Koy" (su altı)

> Tek kaynak durum panosu. Plan: `docs/koy/PLAN.md` (kararlar, fazlar, duraklar). Veri ve denetimleri: `docs/koy/VERI.md`.
> Gerçek fotoğraflardan başvuru panosu: `docs/koy/BASVURU.md`. CC0 taramalar: `assets/koy/fetch.py`, `assets/koy/LICENSES.md`.

## Şu an · 2026-10-03 gece · 🛑 Durak 1 (netlik ve kararlılık) Özcan'ı bekliyor

K0 bitti, K1 (deneme alanı) bitti. Koy kurulmadı: plan gereği Durak 1 geçilmeden koy kurulmaz.

### Durak 1'de neye bakılacak (Özcan, yalnız uygulamanın içinden, canlı)

Açmak: `build\pc_graphics.exe` → galeride **05 Koy** (ya da `pc_graphics.exe --scene koy`). İlk açılış ~5 sn (ışık
alanı hesaplanıyor). Tuşlar: **W A S D** yüz, **Boşluk** yukarı, **C** aşağı, **Shift** güçlü vuruş, **Tab** fareyle bakış
(aç/kapa) ya da **sağ tık basılı** bak, **P** zamanı durdur (durunca 256 örneklik resim birikir), **E** kenar piksellerini
göster, **K** kostik aç/kapa, **↑↓** pozlama.

1. **Dur ve bak (5-10 sn):** dalgalar, kostik ağı, ışık sütunları, çayır akıyor; kamera durgun. Kumda, kaya kenarında,
   çayırda, yüzeyin alt yüzünde karıncalanma, dalgalanma, titreşme var mı? (Ölçüde Fener'den ~15 kat sakin.)
2. **Yavaşça yüz ve dön:** W ile kayaya, çayırın kenarına doğru; Tab ile fareyle yavaşça dön. Kenarlar (kaya silueti,
   çayır yaprakları, kumla kaya temas çizgisi) kayarken tırtıklanma ya da sürünme var mı?
3. **Yukarı bak:** yüzeyin alt yüzü, ayna gibi tam iç yansıma ve tepede Snell penceresi; pırıltılar titreşiyor mu?
4. **Panel düğmeleri** (sol alt): **Örnek** 1 / 4 / 8 / 16 (piksel başına sabit örnek), **Kenar** 4 / 8 / 16 / 32 (kenardaki
   piksele toplam), **Süzgeç** Kutu / Blackman-Harris / Mitchell / Gauss. Varsayılan 4 + 16 + Blackman-Harris. 1 örnek ile
   4'ü kıyasla: fark netliğin kendisi. Kare hızı sol üstte (RivaTuner) ya da panelin sağ üstünde.
5. **P (dur):** zaman durunca bir an sonra 256 örneklik resim gelir; canlı resimle farkı buradan görülür.

Karar istenenler: (a) varsayılan örnek ve süzgeç yeterince net ve sakin mi; (b) kenar 16 mı 32 mi (32: ~41 fps sınırında);
(c) bir şey göze batıyorsa ne ve nerede.

## Yapılanlar

### K0 · ölçüler hazır
- **Ölçü düzeneği** (`tools/kararlilik.py`): bir kamera yolunun canlı kareleri (pencerenin çizdiği yoldan, aynı örnek,
  aynı süzgeç) ve aynı anların yakınsamış referansı. *Kıpırtı:* canlının kareden kareye değişiminden referansın kendi
  değişimi düşülür (kamera ya da dünya kıpırdarken gerçek değişim cezalanmaz), piksel ve 8×8 blok düzeyinde. *Netlik:*
  FLIP (NVIDIA). *Bağıl kıpırtı:* kıpırtı / referansın değişimi. Isı haritası da yazılır.
- **Fener tabanı** (Fener'e `--kararlilik` eklendi; canlı: TAA + titreşim + göz uyumu; referans: 128 kare birikim):

  | Yol | Piksel kıpırtısı ort / %99 | 8×8 blok ort / %99 | Bağıl | FLIP |
  |---|---|---|---|---|
  | Fener A (kamera durgun, huzme döner) | 0,0053 / 0,050 | 0,0011 / 0,0125 | 1,24 | 0,038 |
  | Fener yolculuk (orman içi yürüyüş) | 0,0061 / 0,080 | 0,0026 / 0,036 | 0,45 | 0,026 |

  Kıpırtı Fener A'da denizdeki ay pırıltılarında, yolculukta ağaç gövdelerinin kenarında ve uzak ormanda toplanıyor
  (ısı haritası). **Hedef (K0'da kondu):** Koy her ölçüde Fener'in yarısı ya da daha iyi.
- **Veri** (`docs/koy/VERI.md`): beş araştırma ve iki bağımsız denetim. Pope-Fry'ın 140 değeri, Solonenko-Mobley Jerlov
  tipleri, Austin-Petzold Kd, Fournier-Forand, Zaneveld-Pegau görüş ilişkisi, ASTM G173 güneş tayfı, kabarcık yükselme
  hızları (Haberman-Morton) ve Bühlmann ZHL-16C. `koy_data.h` bu JSON'dan betikle üretildi (elle aktarım yok).
- **CC0 indirme betiği** `assets/koy/fetch.py`: adres + SHA-256; kum (ambientCG Ground054), kaya (Poly Haven boulder_01).
  K2 için seçilmiş olanlar (çakıl, iki kaya yüzeyi, iki kaya modeli) `VERI.md`'de.
- **Başvuru panosu** `docs/koy/BASVURU.md`: 35 gerçek fotoğraf (bağlantı + not), on bölge; her biri indirilip bakıldı.
- **DLSS / DLAA denemesi:** araştırıldı, bağlanmadı. CUDA yolu yalnız **DLSS Ray Reconstruction**'da var (SDK 310.9.1,
  sürücü 590+; NVIDIA'nın OptiX örneği `optix-subd` varsayılanı DLAA). Super Resolution'ın (DLAA dahil) CUDA yolu yok
  (yalnız D3D11/12 ve Vulkan). Ray Reconstruction zamansal ve yapay zekâlı bir gidericidir: planın "zamansal tahmin yok"
  ilkesine ve Özcan'ın TAA yargısına ters. Ana yol kaba kuvvet süper örnekleme; istenirse Durak 1'den sonra yan yana
  kıyas için bağlanabilir (lisans: RTX SDK, DLL betikle indirilir, depoya girmez).

### K1 · deneme alanı
Çizici `src/scenes/koy/` (mimari özeti `PLAN.md` §5'teki gibi; aşağıda yalnız plandan ayrılanlar ve ölçümler).

- **Örnekleme:** her pikselde aynı sabit alt piksel örnekleri (4: döndürülmüş ızgara; 8, 16: Direct3D'nin MSAA desenleri),
  titreşim yok, geçmiş yok. Kenar piksellerine (örnekler başka yüzeye düşüyor ya da görüntü karşıtlığı > 0,04) 32'lik
  desenin geri kalanı; yeniden kurma süzgeci 1,5 piksel yarıçaplı Blackman-Harris, yoğunluk dengeli (kenar pikselinin
  fazla örneği komşulara fazla ağırlık bindirmez). Doku süzme ışın konisiyle (anizotrop).
- **Tayf:** bütün dalga boyu işi hazırlıkta, 380-780 nm, 5 nm adımla: su (Pope-Fry + Jerlov IB'nin su dışı soğurması,
  Haltrin saçılması), güneş ve gök (G173, Preetham-Perez gök biçimi), göz (CIE 1931). Sonuç, ışığın sudaki yol
  uzunluğuna göre XYZ tablolarına pişer; canlı karede yalnız tablo okunur. Yüzey rengi üç pürüzsüz yansıtma tayfının
  ağırlığıdır (gün ışığında birebir geri döner).
- **Işık alanı (fırın):** 1B su sütununda Monte Carlo (kum dipte, yüzeyden Fresnel ve tam iç yansıma); 81 dalga boyu ×
  300 bin foton, ~1 sn. Güneşin tek saçılması ışık sütunu yürüyüşünde, daha yüksek dereceler tablolarda (çift sayım yok).
- **Yüzey:** Tessendorf FFT (512², 16 m, kendi FFT çekirdeğimiz), frekanslar 24 sn'lik döngüye nicelenmiş; LEAN eğim
  piramidi; alttan Snell penceresi, Fresnel, pırıltı (mikroyüz BTDF, güneş diski + izdüşüm alanındaki eğim varyansı),
  tam iç yansıma aynası (yansıyan ışın izlenir).
- **Kostik hacmi:** her karede 1024² güneş ışını kırılır, logaritmik aralıklı 32 derinlik diliminde (0,5-20 m) çatal
  yayılır, güneş diskinin derinlikle bulanıklığı uygulanır; ışık sütunları aynı hacimden.
- **Işık sütunları:** 2×2 piksel bloğu başına bir kez, 32 sabit uzaklık düğümünde yürünür (3B dokuya); her örnek kendi
  uzaklığını tek süzgeçli okumayla alır. Piksel başına yürüyüşe göre 11 → 1,5 ms.
- **Dünya:** kum (ambientCG Ground054, ×1,5 ton), taranmış kaya (Poly Haven boulder_01, 2,8 m), ~36 bin yapraklık çayır
  (düz kuadratik B-spline şeritler, ölü dalga akıntısıyla salınır, kinematik), kumda dalgacıklar (dünya uzayı normal
  haritası, mip'li). Fırınlanmış ortam kararması (köşe başına 128 ışın), sudaki güneş görünürlüğü hacmi (10 cm).
- **Dalgıç:** ~90 kg + eklenen kütle, ikinci dereceden direnç (CdA 0,25), palet itkisi 32 N (Shift 85 N): seyir
  ~0,5 m/sn. Taban, kaya ve yüzeyin 1 m altı sınır. Maske: düz cam (su altında her şey 4/3 yakın, görüş daralır).

### Plandan ayrılanlar (gerekçesiyle)
- **Su tipi IB** (plan "kristal Ege, I-IA"): Solonenko-Mobley değerleriyle IA'da görüş ~70 m ve parçacık saçılması
  (b(550) = 0,0035) ışık sütunu verecek kadar değil; IB'de görüş ~45 m (planın haritası 30-40 m'de maviye kaybolmayı
  istiyor), sütunlar görünür. `water.jerlov` ile değişir.
- **Kostik için kısa dalgalar 35 cm'de kesildi:** tam tayfla (20 cm'ye kadar) ağ 3 m'den derinde benek gürültüsüne
  dönüyordu (kısa dalgalar sığda odaklanıp derinde kaotik). Korunaklı, sakin bir koyda kısa dalgalar zaten zayıf; eğimleri
  çözülmeyen pürüzlülüğe (pencere ve pırıltılar) aktarıldı. 20 / 35 / 60 cm yan yana denendi, 35 gerçek fotoğraflardaki
  ağa en yakın. Ayrıca hafif bir ölü dalga (8 ve 5 m).
- **Güneş diski bulanıklığı derinlik × 0,0069** (plan 0,0093 yazmıştı: o havadaki açı; su altında 0,53°/1,333).
- **Kenar örneği varsayılanı 16** (plan 16-32): 32 ile kare ~41 fps sınırında; düğmeyle 32.
- **Kumdaki dalgacıklar ağda değil, normal haritasında:** 9 cm'lik dalgacık 3 cm'lik ağ köşelerinde örneklenince geniş
  moiré bantları yaptı (zamanla görünür olup kaybolan karanlık şeritler). Artık dünya uzayında mip'li bir normal haritası,
  ışın konisiyle süzülüyor (yakında keskin, uzakta ortalanır).
- **Zamansal düğmesi yok** (plan: zamansal açık/kapalı): zamansal birikim hiç kurulmadı; yerine durunca birikim (P).

### Ölçümler (RTX 4080, 2560×1440)
- **Kare aralığı 41 fps sabit** (plan §1): sahne `Host::pace(41)` ister, uygulama tazeleme periyodunun 4 katına kadar
  bekleyip dikey eşitlemeyle takas eder (G-Sync 1'den büyük takas aralığını yok saydığı için yazılım sınırlayıcı;
  `timeBeginPeriod(1)`). Otomatik testte 41,2-41,5 fps; panelde "Kare: 41 sabit / serbest".
- **Kare süresi** (başlangıç bakışı, 4 + 16): dünya 5,5 (kostik 5,0) · ışık sütunları 1,5 · örnekler 6,2 · kenar 2-5 ·
  çözüm 1,5 · ton 0,1 → **~17-19 ms, 53-60 fps**. Kenar 32: ~24 ms (41 fps). Örnek 16: ~25 ms.
- **Kararlılık** (yol başına 100 kare 41 fps'te, referans 256 örnek):

  | Yol | Piksel kıpırtısı ort / %99 | 8×8 blok ort / %99 | Bağıl | FLIP |
  |---|---|---|---|---|
  | Koy sabit (kamera durgun, dünya akıyor) | 0,00054 / 0,0036 | 0,00011 / 0,00057 | 0,096 | 0,0058 |
  | Koy yüz (0,5 m/sn ileri) | 0,00076 / 0,0064 | 0,00014 / 0,00076 | 0,119 | 0,0060 |
  | Koy dön (12°/sn) | 0,00079 / 0,0064 | 0,00014 / 0,00078 | 0,092 | 0,0059 |
  | *Fener A (taban)* | *0,0053 / 0,050* | *0,0011 / 0,0125* | *1,24* | *0,038* |
  | *Fener yolculuk (taban)* | *0,0061 / 0,080* | *0,0026 / 0,036* | *0,45* | *0,026* |

  **Hedef tuttu:** her ölçüde Fener'in yarısından iyi; piksel kıpırtısında ~7-10 kat, 8×8 blokta ~8-25 kat, netlikte
  (FLIP) ~4-6 kat. Not: Fener'in kıpırtısının bir kısmı her karede değişen titreşimden (±1/255); Koy'un titreşimi sabit
  desen. Blok ölçüsü titreşimi ortaladığı için en adil kıyas o.

- **Fizik testleri** (`--test`): Snell penceresi 48,19° (beklenen 48,61 ± 0,5) GEÇTİ · su ve Fournier-Forand faz
  integralleri 1 · kesik fazın geri saçılım payı beklenenle aynı · ışık alanının derindeki Kd'si (10-35 m) yayımlanan
  IB'ye %2-12 yakın (sınır %15; mavide fark büyük: Kirk ilişkisi öfotik katman ortalaması, mavide o katman ~70 m) ·
  kostik her dilimde enerjiyi korur (ortalama 1 ± 0,02).
- Başvuru panosundaki ölçü: gerçek fotoğraflarda kumdaki kostik p99/p50 ≈ 2,6-3,6 ("neon ağ değil"); bizimki henüz
  ölçülmedi (Durak 2'nin işi).

## NE DOĞRULANMADI
- **Canlı görünüm Özcan'ın gözüyle:** her şey komut satırı kareleri ve otomatik testle; Durak 1 bunun için.
- Kararlılık yalnız üç yolda ölçüldü; çayırın içinden yüzme, yukarı (pencereye) bakarak dönme ölçülmedi.
- Görüş testi (siyah hedef, 4,8/alfa) ve kostik bulanıklığı testi yazılmadı; kabarcık, yüzerlik, Bühlmann testleri K3.
- Çayır yapraklarında ışık geçirgenliği yok (arkadan aydınlanan yaprak koyu kalıyor); uzak çayırın yoğunluk örtüsüne
  geçişi (plan §5.1) yok: Durak 1'de çayırın kendisi en zor kararlılık sınavı olarak duruyor.
- Fırınlanmış ışık sondaları (DDGI) yok: dolaylı ışık 1B ışık alanı + ortam kararması. Kayanın kuma yansıyan ışığı yok.
- Gölgeler güneşin ortalama kırılma yönünde tek ışınla (sert kenar); dalgaların gölgeyi oynatması yok.
- `--autotest-only koy` çalıştı (yükleme, yüzme, bakış, panel, durdurma), ekran görüntüleri `captures/arayuz/koy_*`.
