# PLAN · sahne 5: "Koy" (su altı)

> Tek kaynak plan. 2026-10-03 akşamı Özcan'la uzun bir konuşmayla yazıldı; konuşmanın tamamı hafızada
> `project_grafik_fikirler.md` (hafıza arşivinde) "Su altı sahnesi" bloğunda. Durum ve ölçümler `docs/koy/DURUM.md`'de (açılınca).
> Bağlam sıkıştırılırsa önce bu dosyayı, sonra hafızada `genel/feedback_gpu_ve_grafik.md` §14, §19, §20'yi oku.

## 1 · Görev ve Özcan'ın sözleri

Önceki sahneler dar alanda bir şeyi çok iyi yapmayı hedefledi. Bu sahnenin dar alandaki hedefi: **su altı gibi iyi
yapılması zor bir görüntüyü en iyi şekilde yapmak.** Özcan (2026-10-03):

- *"Suyun altı oyunlar ve grafik olarak neredeyse hiç iyi yapılamayan bir şey. [...] Hyperrealistik ve fizik tabanlı su
  altı hiç hatırlamıyorum. Biz yapabilir miyiz."*
- *"Amaç hep senin yapabileceğin en son sınır, zirvelere ulaşmak."* · *"Bütün bu aldığımız kararlar sadece yön. Tamamen
  özgürsün [...] Ben sonuca bakıyorum."*
- Sonuç: **cam gibi net ve stabil, hiper gerçekçi, estetik olarak çok güçlü ve etkileyici, fizik olarak gerçekçi su altı
  dünyası.**
- *"Netlik ve stabil görüntü o kadar önemli ki detaydan, fizikten ve ışıktan bile kısılabilir bunun için. [...] Fener
  burada yarış değil, en kötü fener olmalı."*
- Netlik çıtası, kendi gördükleri: Metro Last Light Redux 4K + SSAA 4x (*"en ufak bir karıncalanma veya dalgalanma
  yoktu"*), RDR2 4K + yüksek çözünürlük ölçeği (15-20 fps, *"muazzam"*), CoD MW Remastered 16x MSAA (*"inanılmaz netti"*).
  Gözlemi: 4x SSAA, 16x MSAA'dan güzel; TAA, FXAA, SMAA *"çok kötü"*.

### Kararlar (Özcan ya da onun bıraktığı yetkiyle ben, 2026-10-03)
| Konu | Karar |
|---|---|
| Alan | **Tek alan**, bütün emek oraya: Akdeniz kayalık koyu, 3-15 m, öğle güneşi |
| Su üstü | **Yok.** Yüzeye çıkılamaz; yüzey fiziksel olarak var (alttan Snell penceresi, kostik, sütunlar) |
| Kural | **Işık + temas gerçek fizik**, geri kalan basit hesap (amaç optimizasyon). Fizik ne derse o, oyun değil |
| Görüntü | **Netlik ve kararlılık her şeyin önünde.** Fener taban. Kaba kuvvet süper örnekleme, zamansal tahmin yok |
| Hız | **41 fps sabit** (ekran 164 Hz: her 4. tazeleme). 30-40 aralığı kabul, arası görsele |
| Kamera | **Birinci şahıs, tüplü dalgıç**, WASD ile yüzme |
| Yelek | **Otomatik, gerçek fizikli** (elle yok) |
| Gece | **Tuşla gece/gündüz, sahneyi sıfırlar.** Gece fener ve yakamoz |
| Canlılar | **Yalnız uzakta siluet balık sürüleri.** Yakın plan canlı yok |
| Ses | **Yok** |
| Malzeme | **CC0 taranmış malzeme serbest** (ticari değil, herkese açık yayınlanacak) + üstüne kodla katmanlar |
| Hazırlık | **Fırınlama:** değişmeyen ışık sahne açılmadan hesaplanır, diske önbelleklenir; canlı ışık da var |
| Ad | **Koy** (İngilizce "Cove") |
| Bakış | Özcan **yalnız uygulamanın içinden, canlı** bakar (*"fotoğraf yada video değil. Çünkü yanıltıcı olur"*), 2-3 durak |

## 2 · Öncelik sırası (bir şey çatışırsa)

1. **Netlik ve kararlılık:** kamera yüzerken ve dururken; kenarda, yüzeyin içinde, uzakta.
2. **Hiper gerçekçilik ve estetik:** gerçek fotoğrafla yan yana durabilmek ve göze güzel gelmek.
3. **Fizik:** ışık ve temas gerçek; gerisi fizikten türeyen ucuz formül.
4. **Ayrıntı:** taranmış malzeme, kodla katmanlar, örnekleme.
5. **Kare hızı:** 41 fps sabit; 30'un altına inilmez.

Sanat için fizikten sapılan her yer kodda ve DURUM'da **adıyla işaretlenir** (Fener kuralı).

## 3 · Görsel hedef ve sanat yönetimi

**Bir bakışta göz ne görüyor?** *Öğle güneşinin altında kristal berraklıkta bir koy: dipte dans eden ışık ağları, tepede
parlayan Snell penceresi, kayaların arasından aşağı inen ışık sütunları, uzakta her şeyi yutan mavi.*

Okuma sırası: (1) yukarıdan inen ışık (sütunlar, pencere), (2) dipteki kostik ağı, (3) kayalık resifin renkleri,
(4) mağaranın karanlık ağzı, (5) açık denize doğru maviye kaybolan boşluk.

**Kompozisyon:** başlangıçta dalgıç ~5 m'de, koyun ortasında, koyun dibine (mağaraya) bakar: önde kum açıklığı ve
kostik, solda kayalık resif duvarı, sağda çayır, ortada uzakta mağaranın ağzı, tepede yüzey. Arkada (dönünce) açık deniz:
kum yamacı aşağı iner, 30-40 m'de maviye kaybolur.

**Renk:** turkuazdan maviye. Sığda sıcak kum rengi biraz kalır, 10 m'de kırmızı neredeyse gitmiştir. Doygun renkler
yalnız yakında (resifin turuncu süngerleri, mor yelpaze mercanları, pembe kireçli yosun kabuğu); uzaklık her şeyi
maviye çeker. Bunların hepsi tayfsal soğurmadan **kendiliğinden** gelir, boyanmaz.

**Hareket:** yüzey dalgalanır, kostik akar (kımıl kımıl değil, akıcı), çayır dalga akıntısıyla ileri geri salınır,
kabarcıklar yükselir, uzakta balık sürüleri döner. Durağan olan her şey kaya gibi durur.

**Gece:** Ay Snell penceresinden süzülür, asıl ışık dalgıcın feneri; fenerin önünde asılı tanecikler, hareket ettikçe
yakamoz. Mağara gece büsbütün başka bir yer.

## 4 · Koyun haritası

Ölçüler gerçek metre. Kabaca **120 × 80 m**, derinlik 3-15 m, açık deniz tarafı 30-40 m'ye iner.

| Bölge | Yer, derinlik | İçerik |
|---|---|---|
| Başlangıç | ortada, ~5 m | asılı dalgıç, koya bakar |
| Kum açıklığı | ortada, 5-9 m | dalgacıklı ince kum, çakıl lekeleri, kostik |
| Çayır | sağ yamaç, 6-12 m | deniz çayırı (Posidonia benzeri şeritler), arasında kum kanalları |
| Kayalık resif | sol duvar ve kaya adacıkları, 3-14 m | taranmış kaya + yosun örtüsü, kireçli kabuk, midye, deniz kestanesi, sünger, yelpaze mercanı, anemon, çatlak ve oyuklar |
| Mağara | koyun dibi, ağzı ~8-12 m | 20-30 m derinliğe giren tünel; tavanında bir baca: içeri dikey ışık sütunu; tavanda dalgıç kabarcıklarının biriktiği gümüş hava cepleri |
| Açık deniz | arka, 15 → 40 m | kum yamacı, seyrek kaya, maviye kaybolur; uzakta siluet sürüler |
| Yüzey | üstte | dalgalı, alttan ayna (tam iç yansıma) + Snell penceresi; pencerede gök ve kayalık kıyının kaba siluetleri |

**Sınırlar:** yanlarda ve arkada kayalık duvarlar ve derinlik doğal sınır; açık deniz tarafında görüş zaten 30-40 m'de
biter, oradan sonrası dolgu gerektirmez. **Tavan:** yüzeyin ~1 m altı; otomatik yelek seni orada tutar, sert duvar yok.
Kuşbakışı harita ürünle **aynı kaynaktan** çizilir (`--map`; Fener dersi, hafıza `genel/feedback_tasarim_ilkeleri.md` TEK KAYNAK).

## 5 · Çizim mimarisi

Yeni bir çizici, `src/scenes/koy/`. Yol izleyici değil: **görünürlük ışın izlemeyle, ışık olabildiğince deterministik.**
Rastgelelik yalnız fırında (hazırlıkta) kalır.

### 5.1 Örnekleme (netliğin kendisi)
- **Her yerde 4 tam örnek** (renk de dört kez): gerçek 4x SSAA. Alt piksel konumları sabit, iyi dağılmış (dönmüş ızgara
  ya da 4 noktalı en iyi desen); her kare aynı desen, titreşim yok.
- **Kenarda 16-32 görünürlük örneği** (MSAA'nın ışın izleme karşılığı): ilk 4 örneğin çarptığı yüzeyler farklıysa
  piksele ek görünürlük ışınları atılır; pikseldeki her ayrı yüzey bir kez gölgelendirilir, kapsadığı payla karışır.
- **Renk farkı büyükse ek gölgeli örnek:** parlama, kostik kenarı, ince çayır; 4 örneğin varyansı eşiği aşarsa 8-16.
- **Yeniden kurma süzgeci** pikselden geniş (Blackman-Harris ya da Mitchell, yarıçap ~1,5 piksel), kutu değil.
- **Zamansal birikim varsayılan kapalı.** Gerekirse yalnız artık için, hareket vektörü fizikten kesin, bulanıklık ve iz
  bırakırsa kapalı kalır. **Durunca** örnekler birikir (fotoğraf kalitesine çıkar), kamera ya da dünya kıpırdayınca sıfırlanır.
- **Doku süzme ışın konisiyle** (uzaklık ve eğikliğe göre doğru mip), **parlama yumuşatması** (pürüzlülük, normalin
  piksel ayak izindeki değişimine göre artırılır: Toksvig / Kaplanyan-Hill).
- **Pikselden ince şey bırakılmaz:** uzak çayır şerit yerine yoğunluğu doğru bir örtüye döner; çok küçük asılı
  tanecikler en az bir piksel ayak iziyle, yumuşak çizilir ya da hiç çizilmez (netlik > ayrıntı).
- **Canlı kalite düğmesi** (Durak 1 için): örnek sayısı 1/4/8/16, kenar örneği 4/8/16/32, zamansal açık/kapalı, fps.

### 5.2 Işığın bölünmesi
| Bileşen | Nasıl | Gürültü |
|---|---|---|
| Güneş (doğrudan) | gölge ışını + yüzeyden geçiş (Fresnel) + kostik hacminden ışınım | yok |
| Gökten gelen (pencereden) | fırınlanmış gök görünürlüğü × canlı yüzey geçişi | yok |
| Dolaylı (yüzeyler arası sekme) | **fırın:** ışık sondası ızgarası (görünürlüklü, sızmaya karşı) + gerekirse ışık haritası | yok |
| Sudaki tek saçılma (pus, sütunlar) | görüş ışını boyunca: homojen kısım kapalı formül, sütunlar kostik hacminde sabit adımlı yürüyüş | yok (sabit adım + mavi gürültü yalnız adım kaydırmada) |
| Sudaki çoklu saçılma | **fırın:** düşük frekanslı saçılma hacmi | yok |
| Fener (gece, mağara) | koni ışık, gölge ışını, sisteki saçılması kapalı formül (eşit açılı) | yok |
| Yansıma, kırılma | canlı ışın izleme (ıslak taş, kabarcık, yüzeyin alt yüzü) | yok |

### 5.3 Tayf
**8-12 sabit bant** (400-700 nm), her bant her örnekte birlikte hesaplanır; rastgele dalga boyu yok (renk greni yapar).
Sonunda `shared/spectrum.h`'nin CIE eğrileriyle XYZ → sRGB. Taranmış RGB dokular tayfa çevrilir (Jakob-Hanika 2019
sigmoid tablosu ya da düzgün bir taban). Su: **saf su soğurması** (Pope ve Fry 1997) + **Jerlov su tipi** IOP'leri
(Solonenko ve Mobley 2015, kristal Ege için tip I-IA); saçılma faz fonksiyonu Fournier-Forand ya da Petzold.

### 5.4 Su yüzeyi
Tessendorf FFT, hafif rüzgâr spektrumu (JONSWAP), **döngülü** (frekanslar periyoda kuantize: dalgalar 20-30 sn'de kusursuz
tekrar eder). Periyodik yama (~16-32 m) koya döşenir, ikinci bir yama ile tekrar kırılır. Alttan: Fresnel + tam iç
yansıma (48,6° ötesi ayna), pencere içinde kırılan gök ve kıyı siluetleri. Normaller piksel ayak izine göre önceden
süzülür (yüzeyin alt yüzü parıldamasın).

### 5.5 Kostik ve ışık sütunları
Her karede yüzeyden çok sık bir ışın ızgarası kırılır; ışınlar **derinlik dilimlerinde** (örneğin 0-16 m, 32 dilim)
yatay düzlemleri keser ve ışınım dilime işlenir: yamaya göre periyodik bir **kostik hacmi** (x, y, derinlik). Güneş
noktasal değil, 0,53°'lik disk: her dilim derinlikle orantılı bulanıklaştırılır (10 m'de ~9 cm). Bu hem fiziksel hem
kararlılık için iyi: kostik keskin ama titremez. Yüzeyler ışınımı hacimden okur (kaya duvarları ve çayır dahil); ışık
sütunları görüş ışını boyunca aynı hacimden yürünerek çıkar. Mağara içi: bacadan giren ışık aynı yöntem, bacanın
ağzından başlayan ayrı küçük bir hacim.

### 5.6 Fırın (hazırlık)
Sahne açılırken (`load()` adımları, yükleme ekranı) ya da önbellekten:
- Işık sondası ızgarası: kabaca 1 m aralık, mağarada ve kaya diplerinde sık; her sondada bant başına küresel harmonik
  ve görünürlük (DDGI tarzı uzaklık momentleri: ince duvardan ışık sızmasın). Binlerce örnekle yol izlenir.
- Sudaki çoklu saçılma hacmi, kaya yüzeylerinde gök görünürlüğü (ortam kararması).
- Ayrıntı yerleşimi ve malzeme katman maskeleri (5.7), yoğun geometri (5.8).
- **Önbellek** `cache/koy/<özet>/`: girdilerin özeti (geometri tohumu, ayarlar, malzeme dosyaları, kod sürümü) aynıysa
  saniyeler; değişince yeniden. Gece ayrı fırın. Git'e girmez.

### 5.7 Malzemeler
- **Taban:** CC0 taranmış kaya, kum, çakıl (Poly Haven, ambientCG): renk, pürüzlülük, normal, yükseklik. İndirme betiği
  adres + SHA-256 ile (`third_party/fetch.sh` gibi), dosyalar git'e girmez; lisans dosyası `assets/koy/LICENSES.md`.
- **Hayat katmanları kodla, geometriden türeyen kurallarla** (hazırlıkta hesaplanır): yukarı bakan yüzeyde **çökelti
  tozu**; ışığa bakan yüzde **yosun örtüsü** (derinlikle azalır, renk değişir); derin ve gölgede **pembe kireçli kabuk**;
  çatlak ve oyukta **midye, deniz kestanesi**; çıkıntıda **yelpaze mercanı, sünger, anemon**. Girdiler: eğrilik, yön,
  gök görünürlüğü, güneş maruziyeti, derinlik.
- **Örnekleme (instancing):** midye, kestane, çakıl, sünger bir kez modellenir, yüz binlerce kopya (Küp'te 1 milyon
  kopya 15-20 ms'de çizildi).
- Islak yüzey: suyun içinde "ıslaklık" kırılma indisi farkıdır; taş-su arayüzü n = 1,33'e göre Fresnel.

### 5.8 Geometri
- Arazi ve kayalar: işaretli uzaklık alanından (SDF) üretilen yoğun üçgen ağları, taranmış yükseklik haritasıyla
  yer değiştirme; mağara SDF'den oyulur. Donanım üçgen ışın izleme; çok yoğunlaşırsa OptiX 9.1 küme hızlandırma yapıları.
- Çayır: **OptiX yassı eğrileri** (`OPTIX_PRIMITIVE_TYPE_FLAT_QUADRATIC_BSPLINE`, şerit genişliği değişken); yakındakiler
  her karede güncellenir (yerinde yeniden kurma), uzaktakiler ayrı ve seyrek.
- Yelpaze mercanı: dallanan yuvarlak eğriler (kodla, uzay kolonizasyonu ya da L-sistemi).
- Çarpışma: durağan geometrinin kaba SDF'i (dalgıç kapsülü için ucuz sorgu).

### 5.9 Kamera ve görüntü
- **Fiziksel kamera:** metre, pozlama EV, derinleşince **yarım göz uyumu** (Fener), filmik ton eğrisi, 8 bit çıktıda TPDF
  titreşim (Inside kuralı, bantlanma yok).
- **Maske optiği:** düz maske camı: su altında açılar n = 1,33 ile kırılır, her şey ~4/3 büyük ve yakın, görüş açısı
  daralır; izdüşüm buna göre. Maskenin çerçevesi çizilmez.
- **Hareket bulanıklığı:** gerçek deklanşör kadar, çok hafif; Durak 1'de açık/kapalı denenir, netliğe zarar verirse kapalı.
- Parlama (bloom): fiziksel ve kısık; yalnız güneş penceresi ve fener için.

## 6 · Fizik (ışık + temas gerçek, gerisi basit)

- **Dalgıç:** kapsül cisim (~90 kg donanımla), ikinci dereceden su direnci, palet itkisi; WASD bakılan yöne, Boşluk
  yukarı, C aşağı, Shift güçlü vuruş; fare bakış. Kafa sallanması yok. Durağan geometriye kapsül-SDF çarpışması.
- **Yüzerlik:** elbise neopreni kapalı hücreli köpük: gaz hacmi Boyle'a göre basınçla küçülür (15 m'de 2,5 atm).
  **Otomatik yelek:** bir denetim döngüsü yüzerliği nötr tutmak için yeleğe gerçekten hava basar ya da boşaltır (havanın
  hacmi de Boyle'a göre değişir, gecikme gerçek). Tavan ~1 m bu döngünün hedefi.
- **Kabarcıklar:** ~4 sn'lik nefes döngüsü; boy dağılımı; boya göre yükselme hızı (Clift, Grace, Weber 1978 rejimleri:
  küre, salınan elipsoit, küresel başlık), yükseldikçe **Boyle ile büyüme**, büyükleri parçalanır. Tavana (mağara,
  kaya çıkıntısı) takılanlar birleşip **gümüş hava cebi** olur. Çizim: kırılma + tam iç yansıma (2 ışın, deterministik).
- **Çayır ve temas:** her şerit 6-8 parçalı XPBD ipi; kaldırma, direnç, **dalga akıntısı** (yüzeyin aynı FFT'sinden,
  derinlikle sönen yörünge hızı). Dalgıcın kapsülü ve paletleri değer; dalgıcın izi (basit akış alanı) dokunmadan da
  iter. Yakın ~8 m tam fizik, uzaktakiler aynı akıntı formülüyle, temassız.
- **Kum bulutu:** palet dibe yakınsa dipteki kayma hızı eşiği aşar, ince kum kalkar; tane boyuna göre Stokes hızıyla
  çöker. Nokta değil, yumuşak yoğunluk hacmi olarak çizilir (kararlı).
- **Siluet balık sürüleri:** uzakta (> 15 m), sürü kuralları (boids), dalgıçtan kaçar; düşük ayrıntı, su zaten yumuşatır.
- **Yakamoz (gece):** dalgıcın çevresinde bir hacimde tanecikler; dalgıcın izindeki kayma eşiği aşınca 0,1-0,3 sn
  parlar. Pikselden küçük parlak nokta kıpırtı demek: yumuşak, en az bir piksel ayak izi.
- **Dalış bilgisayarı** (sağ üst ölçüm şeridi): derinlik, basınç, süre, tüp basıncı (tüketim × P), dekompresyonsuz
  süre (Bühlmann ZHL-16C), çıkış hızı uyarısı.

## 7 · Gece, sinematik tur, tuşlar

- **Gece/gündüz:** tuşla (N), sahneyi sıfırlar (fırın önbellekten). Gece: Ay + fener (F).
- **Sinematik tur:** (T) koyun içinde 60-90 sn'lik yumuşak bir yol (mağaraya girip çıkar). Hem gösteri hem ölçüm: kararlılık
  ve kare hızı hep bu yoldan ölçülür; otomatik test (`--autotest-only koy`) de bu yolu kullanır.
- Kalite düğmeleri panelde (5.1).

## 8 · Kare bütçesi (41 fps = 24,4 ms; ilk ölçümden sonra düzeltilir)

| İş | ms |
|---|---|
| Görünürlük: 4 örnek + kenar ek ışınları | 4,0 |
| Gölgelendirme ×4 (+ uyarlamalı ek) | 7,0 |
| Su: pus + ışık sütunları (piksel başına bir kez) | 3,0 |
| Kostik hacmi + yüzey | 2,0 |
| Fizik: çayır, dalgıç, kabarcık, kum, sürüler | 2,0 |
| Son işlem: süzgeç, ton, parlama | 1,0 |
| Arayüz, kopya | 0,5 |
| **Yedek** | 4,9 |

## 9 · Doğrulama ve başarı kartı

**Kararlılık ve netlik (her değişiklikte, sinematik tur yolunda, canlı yoldan; §19 dersi):**
- *Kıpırtı:* aynı yol iki kez, kare kare 8×8 blok parlaklığının zamansal sapması (Laboratuvar revize 1 ölçüsü).
- *Keskinlik:* canlı kare ↔ aynı anın 256 örnekli referansı (FLIP); bulanıklaştırarak "kararlı" olmak ölçüde yakalanır.
- *Taban:* aynı ölçüler **Fener'in** turunda alınır; Koy Fener'den açıkça iyi olmalı (hedef değer K0'da konur).

**Fizik testleri (`--test`):**
| Test | Ölçüt |
|---|---|
| Snell penceresi | yarım açı 48,6° (n = 1,333) ± 0,5° |
| Derinlikte renk | dip ışınımının tayfı, seçilen Jerlov tipinin Kd'sine karşı |
| Görüş | siyah hedefin görünürlük uzaklığı ≈ 4,8 / c (Zaneveld ve Pegau 2003) |
| Kostik bulanıklığı | derinlik × 0,0093 (güneş diski) |
| Kabarcık | 1, 3, 10 mm yükselme hızı yayımlanmış eğriye karşı; Boyle büyümesi |
| Yüzerlik | elbisenin derinlikle kaybı; yelek nötr tutuyor mu, gecikme |
| Bühlmann | dekompresyonsuz süreler yayımlanmış ZHL-16C tablosuna karşı |
| Çayır | temassız salınım genliği akıntıyla tutarlı; temasta geri dönüş, enerji sönümü |

**Görünüş:** her bölge için gerçek su altı fotoğraflarından başvuru panosu (yalnız bağlantı ve not, depoya resim girmez);
duraklarda Özcan canlı bakar.

**Hız:** turda 41 fps sabit; kare süresi dağılımı, en kötü %1.

## 10 · Fazlar ve duraklar

| Faz | İş | Sonu |
|---|---|---|
| K0 | Sahne iskeleti (kayıt, `koy.cfg`, galeri yeri), veri (Pope-Fry, Solonenko-Mobley, kabarcık eğrileri, Bühlmann), CC0 indirme betiği, başvuru panosu, **ölçü düzeneği** (kıpırtı + keskinlik) ve **Fener tabanı**, DLSS/DLAA denemesi (NGX'in CUDA yolu; olmazsa kendi yöntemimiz) | ölçüler hazır |
| K1 | **Deneme alanı** (~20 × 20 m): kum, bir taranmış kaya, bir çayır parçası, FFT yüzey, kostik hacmi, sütunlar, su, 5.1 örneklemesi, dalgıç hareketi, kalite düğmeleri | 🛑 **Durak 1: netlik ve kararlılık** |
| K2 | Koyun kendisi: arazi, kayalar, resif, mağara, fırın + önbellek + yükleme adımları, malzeme katmanları, örnekleme | 🛑 **Durak 2: görünüş** (kaya etüdü, resif, mağara, ışık) |
| K3 | Temas ve fizik: çayır tam, çarpışma, yelek, kabarcık ve hava cepleri, kum bulutu, dalış bilgisayarı | |
| K4 | Gece ve yakamoz, siluet sürüler, sinematik tur, otomatik test | |
| K5 | Cilalama: sanat turu ("%90 kıs", en kötü ayrıntı), bütçe, README/GOSTERI/DEMO, galeri resmi | 🛑 **Durak 3: son** |

Duraklarda dururum: Özcan uygulamayı açar, canlı bakar; neye bakacağını kısa yazarım. Aralarda kendim ilerlerim.
Durak 1 geçilmeden koy kurulmaz: netlik tutmazsa önce o çözülür.

## 11 · Riskler

| Risk | Önlem |
|---|---|
| 4x gölgelendirme bütçeyi aşar | gölgelendirme deterministik ve ucuz; uyarlamalı ek örnekler; K1'de ölçülür, gerekirse kenar dışı 2-3 örnek |
| Kostik ve yüzeyin alt yüzü titrer | güneş diski bulanıklığı, önceden süzülmüş normaller, sabit desen; K1'in ana sınavı |
| Uzak çayır, ince şeyler karıncalanır | yoğunluk örtüsüne geçiş, ayak izi kontrolü |
| Fırınlanmış ışık mağara duvarından sızar | görünürlüklü sondalar, mağarada sık ızgara; K2'de sınanır |
| Taranmış malzeme tekrar eder | iki ölçekte karıştırma, katman maskeleri, döndürme; başvuru panosuyla kıyas |
| Yakın ayrıntı fotoğraf olmaz | dürüst sınır: orta mesafe hedef, yakın en iyi çaba; Durak 2'de Özcan'ın gözü |
| DLSS bağlanmaz | gerekmiyor: ana yol kaba kuvvet örnekleme |

## 12 · Kapsam dışı

Su üstü ve yüzeye çıkış, ses, yakın plan canlılar, kelp ormanı, batık, kara, gece/gündüz geçişi (sıfırlamayla değişir),
video dışa aktarma. Hepsi ileride konuşulabilir; tasarım kapı kapatmaz.

## 13 · Çalışma biçimi

- "grafik devam" gelince bu plan doğrudan uygulanır (Özcan 10-03: *"ben grafik devam yazarım. Sen direk planı uygulamaya
  geçersin"*): K0'dan başla, Durak 1'e kadar kendi başına ilerle.
- Yaklaşık 30 dakikada bir commit + push (depo PRIVATE). Commit mesajları Türkçe, oturumun atıf satırlarıyla. Sıra:
  hafıza → `claude_memory` commit/push → proje commit/push.
- **Doğrulanan yol, Özcan'ın gördüğü yol:** her görsel karar canlı pencerede, hareket hâlinde ölçülür; durağan kareyle
  yetinilmez (hafıza `genel/feedback_gpu_ve_grafik.md` §19).
- Paralel ajan varsayılan yok; gerekirse önce Özcan'a sorulur.
- İş özetleri "NE DOĞRULANMADI" ve "hafıza itildi: evet/hayır" ile biter. Uzun çizgi kullanma.

## 14 · Kaynaklar

- Pope, R. M., Fry, E. S. (1997). Absorption spectrum (380-700 nm) of pure water. II. *Applied Optics* 36(33).
- Solonenko, M. G., Mobley, C. D. (2015). Inherent optical properties of Jerlov water types. *Applied Optics* 54(17),
  5392. doi:10.1364/AO.54.005392
- Zaneveld, J. R. V., Pegau, W. S. (2003). Robust underwater visibility parameter. *Optics Express* 11(23).
- Jerlov, N. G. (1976). *Marine Optics.* Elsevier.
- Mobley, C. D. *Ocean Optics Web Book* (oceanopticsbook.info): faz fonksiyonları, Fournier-Forand, Petzold.
- Tessendorf, J. (2001). Simulating Ocean Water. SIGGRAPH kurs notları.
- Clift, R., Grace, J. R., Weber, M. E. (1978). *Bubbles, Drops, and Particles.* Academic Press.
- Bühlmann, A. A. *Tauchmedizin* (ZHL-16C katsayıları).
- Jakob, W., Hanika, J. (2019). A Low-Dimensional Function Space for Efficient Spectral Upsampling. *Eurographics.*
- Majercik, Z. ve ark. (2019). Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields. *JCGT* (DDGI).
- NVIDIA NGX programlama kılavuzu (docs.nvidia.com/rtx/ngx): D3D11, D3D12 ve CUDA desteği.
- Playdead, INSIDE GDC 2016 (Fener'den: TPDF titreşim, "%90 kıs").
