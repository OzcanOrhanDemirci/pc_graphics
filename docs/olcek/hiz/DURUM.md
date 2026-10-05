# DURUM · Ölçek'in ikinci seçeneği: "Hız"

> Tek kaynak durum panosu. Plan `docs/olcek/hiz/PLAN.md`. En yeni en üstte.

## 2026-10-05 gece · H1: grafikler

**Özcan'ın bakacağı yer.** Hız'da beş grafik panosu resmin üstünde, iki görüntüde de. Panelde **Grafikler: Açık |
Kapalı** (ya da **G**).
- Sol üstte HIZ ve YOL, sağda UZAY-ZAMAN ve SAATLER, altta BEDEL.
- Panolar uygulamanın panelleri arasındaki boşluğa yerleşir: sağ üstte ölçümler, sol altta sahne paneli, sağ altta
  tuşlar. Sığmazlarsa birlikte küçülürler. 2560×1440 için çizildiler.

**Panolar** (`src/scenes/olcek/olcek_hiz_pano.h/.cpp`):
1. **HIZ:**
   - Işık duvarına dayanan hız çubuğu.
   - γ(β) eğrisi ve üstünde nokta (logaritmik; ekseni kendini büyütür).
   - Dokuz sayısı başlıkta.
2. **UZAY-ZAMAN** (400×400, Özcan'ın referansı `assets/olcek/referans/uzay-kisalmasi.png`):
   - Dünya'nın eksenleri: x (uzay) ve t (zaman).
   - Roketin 5×5'lik ızgarası, kenarları x′ ve t′ boyunca. Hızlandıkça ikisi de ışık çizgisine doğru yatar (eksenden
     atan β kadar) ve ızgara bir çizgiye döner.
   - Yalnız bu katlanma: uzak zaman ve uzay senin için bükülüp kısalır. Sayıları öteki panolar veriyor.
3. **YOL:**
   - İki cetvel: Dünya'nın ölçtüğü ve senin ölçtüğün (γ kat kısa).
   - Durma mesafesi: altın köşeli çizgi ve yazı.
   - Sıkışan uzay ızgarası.
4. **SAATLER:**
   - İki kadran. Dünya'nınki γ kat hızlı; ibreleri gözün kapağı açıkken taradığı yelpaze, bir tam turda disk.
   - "Dünya'da şimdi (senin için)": t − β(r·m)/c.
   - "Gördüğün Dünya": t − d/c, Doppler çarpanıyla.
5. **BEDEL** (1 tonluk roket):
   - Hareket enerjisi.
   - Foton roketinin yaktığı, "durmak için de yakarsa" ve kütle oranı (durmakla).
   - İvme.
   - Önden gelen ışıma: öne bakan kara plakaya düşen fon ışıması (kapalı biçim).
   - Öndeki fon ışımasının sıcaklığı, rengi ve bandı.
   - Her hidrojen atomunun enerjisi.
   - Dipnot: düşünce deneyi.

**Altyapı:**
- **Sahne sözleşmesine `overlay()`.** Sahne resmin üstüne arayüz takımıyla çizer: uygulamanın panellerinin altında, H ile
  gizlenir. Uygulamanın panel kutuları da (`o.panels`) gelir; grafikler onlardan uzak durur.
- **Yazı yedeği.** Mono'da ve Serif'te olmayan harf (γ, β), aynı kalınlıktaki Sans'tan çizilir. Ekranda "?" çıkmıyor.
- **Kayan denetim tıklama almaz (çift tıklama tuzağı).**
  - Ölçek'te "Hız"a çift tıklama, yerine gelen "Otomatik varış"a basıp yolculuğu başlatıyordu.
  - 2560×1440'ta Ölçek'teki "Hız"ın ortası (244, 1226), Hız'daki "Otomatik varış"ın (161-385 × 1210-1263) içinde.
  - Duran imlecin altına kendiliğinden gelen denetim, yarım saniye geçene ya da fare kıpırdayana kadar tıklama almıyor.
    Bütün sahneler için geçerli. Otomatik test artık "Hız"a çift tıklıyor.
  - Bulunuşu: tam ekran otomatik testin birinde roket kalkış karesinde itiyordu, yani otomatik varış Boşluk'tan önce
    açıktı. Sonra Boşluk onu kapattı; Proxima'ya varılmadı.
  - İkinci tıklamanın kaynağı bulunamadı. Otomatik test o koşuda tek tıklama yaptı; en olası açıklama, bilgisayarın
    başında yapılmış gerçek bir tıklama.
- **Sayı yazımı tek kaynak:** panelin ve panoların sayıları `olcek_hiz_pano.cpp`'den.
  - 1'in altı üç anlamlı basamak.
  - 0,01'in altı 10'un kuvveti.
  - Tam sıfır "0".

**Revize (Özcan'ın ilk bakışı, aynı gece):**
- *"Uzay zaman grafiği [...] Sağda kocaman. Orada bir eğri çizmeye gerek yok. [...] ızgara bükülüp çizgileşiyor. O sade
  his yeterli."* Minkowski diyagramı (Dünya'nın ızgarası, dünya çizgileri, ışık konisi, noktalar) söküldü; yerine
  referanstaki gibi eksenler ve katlanan ızgara. Küçüldü: 520×560'tan 400×400'e.
- *"Kaç tane dokuz olduğunun sayısına bence gerek yok [...] sayısal detay olarak yazıyor zaten."* Dokuz merdiveni
  söküldü.
- Dünya çizgisinin izi (`Trace`) ve diyagramın ölçeği kodun her yerinden çıktı.
- Görüntü üzerine söyledikleri (hız hissi, ivme, yandan bakış) bir sonraki taşın konusu; konuşuluyor.

### Ölçümler
- **`--test olcek-hiz`, hepsi GEÇTİ.** Yeni üç sınama var.
  - BEDEL, 21 dokuzda plandaki sayılar:
    - hareket enerjisi Güneş'in 1,458 saati;
    - foton roketi 2,917 saat, durmakla 14,88 milyon yıl;
    - kütle oranı 4,472 × 10¹⁰ (durmakla 2,000 × 10²¹);
    - ivme 12,23 milyon g;
    - H atomu 2,10 × 10¹⁹ eV;
    - önde fon 1,22 × 10¹¹ K.
  - Önden gelen ışıma Güneş ışığının 17 dokuzda 6,13 × 10⁸ katı, 21 dokuzda 6,13 × 10¹² katı (plan: 6 × 10⁸ ve
    6 × 10¹²).
  - Plaka formülü başka bir yoldan sınandı. Ön yarıküre eksi arka yarıküre, izotrop ışımanın gerilim-enerji tensörünün
    dönüşümüne eşit olmalı: (16/3)γ²βσT₀⁴.
    - En büyük fark 1,7 × 10⁻¹¹.
    - Seriden kapalı biçime geçişteki sıçrama 8,3 × 10⁻⁶.
- **Canlı, tam ekran 2560×1440** (`--autotest-only olcek/hiz`):
  - 160-162 fps (ekran 165 Hz), kare 5,7-6,4 ms;
  - çift tıklamadan sonra Hız 0 m/s;
  - Proxima'ya "senin saatinle 1:39, Dünya'da 4,25 yıl";
  - 153° dönüş.
- **Panoların değerleri elle hesapla tuttu:**
  - γ 33'te önden gelen ışıma 1,34 × 10⁻⁵ (ekranda 1,33 × 10⁻⁵); fon 180 K, Wien tepesi 16 µm, kızılötesi.
  - γ 111'de durma mesafesi 82,5 milyon km (ekranda 82,6).
  - Proxima'da Dünya'da şimdi 3 Ocak 2031. Görülen Dünya kalkış günü, çünkü ışık 4,25 yıl gecikmeli.
- Kareler: `captures/arayuz/olcek_hiz_*.png` (git dışı).

### NE DOĞRULANMADI
- **Özcan'ın gözü:** panolar onun gönderdiği görüntülerin diliyle yan yana konmadı. Renk, yoğunluk ve yerleşim onayını
  bekliyor.
- **Küçük pencere:** 1280×720'de panolar sığmak için küçülüyor; yazılar ~5 piksel, okunmuyor. Küçük pencerede pano
  azaltmak ya da büyütmek düşünülmedi.
- **Çift tıklama düzeltmesinin testi** kırmızıya döndürülerek sınanmadı: düzeltme kapatılıp test koşulmadı. Kanıt,
  hatanın görüldüğü koşu ve düğmelerin ölçülen yerleri.
- **Öteki sahneler:** yazı yedeği ve tıklama koruması bütün sahneleri etkiliyor, ama onların otomatik testleri yeniden
  koşulmadı. Yazıda yalnız eksik harfler değişir (önceden "?"). Tıklamada yalnız duran imlecin altına kayan denetim
  bekler.
- **Minkowski diyagramı:** çok dokuzda roketin ızgarası neredeyse görünmüyor; sıkıştıkça seyreltiliyor. Bu doğru mu,
  yoksa daha görünür mü olmalı: Özcan'ın gözü karar verecek.
- **Bildirimler** (alt orta), sahne paneli genişlediğinde BEDEL'in sağ ucuna kısa bir süre değebiliyor.
- **Fon ışımasının renk lekesi:** 600 K'nin altında saydam (görünür ışık yok). Üstünde en parlak kanala göre
  normalleşiyor, bu yüzden 606 K'de koyu kırmızı değil parlak kırmızı. Leke parlaklığı değil yalnız renk tonunu
  gösteriyor.

## 2026-10-05 gece · H0: fizik ve iskelet

**Özcan'ın bakacağı yer.** Galeride 07 Ölçek → panelde **Seçenek: Ölçek | Hız** → Hız. Sahne her zaman Ölçek'te açılır;
yolculuk Hız'da kaldığı yerde bekler.
- **W** itiş (0,4 c/s, 12 milyon g), **S** fren (aynı güçle, geri vites yok), **Boşluk** otomatik varış.
- **Tab** roketin gözü ↔ yandan. Sürükle: başını çevir. Tekerlek: dürbün (75°'den 10⁻¹¹ radyana) ya da yandan ölçek
  (10 m - 10²³ m). **R** ileri bak / rokete dön. F1'de "Hız" tuş grubu var.
- Panel: yol, hız (m/s; çok dokuzda "c − x m/s"), β (bütün dokuzlarıyla), dokuz sayısı, γ, hız açısı φ, senin saatin,
  Dünya'da geçen, Dünya'da tarih, kalan (Dünya'nın ve senin ölçtüğün), durma mesafesi, görüş açısı / ekranın genişliği.
- "Yolculuğu baştan başlat" iki kez basınca.

**Fizik** (`src/scenes/olcek/olcek_hiz.h/.cpp`). Her şey hız açısı φ'den: β = tanh φ, γ = cosh φ, ve
1 − β = 2/(e^(2φ) + 1); 0,999...9'u double saklayamaz, bu biçim her φ'de basamak kaybetmez. İtişte, frende ve süzülmede
durgun çerçevenin zamanı ve yolu kapalı biçimlerle ilerler; adım hatası yok. Kare içindeki olaylar karede tam yerinde
bölünür: tavana varış, frene geçiş, duruş, dönüşün sonu. Konum ayağın hedefinden çift-double, 10²² m'de metrenin çok
altında kesin. Tavan 21 dokuz (φ = 24,5237).

**Rota:**
- Dünya'nın yanından (bugünkü yeri, yer eşzamanlı yarıçapta) Proxima'ya: Gaia DR3, astropy ile 5 Ekim 2026'ya taşınmış.
  1,301729 pc = **4,2457 ışık yılı**. Roket Proxima b'nin yörünge yarıçapında (0,0486 AU) durur.
- Proxima'da **153,4°** dönüş. 2. ayak Güneş'in **1,90 ışık yılı** yanından geçer, Andromeda'nın 100 bin ışık yılı önünde
  biter: **2,3951 milyon ışık yılı**.
- Kataloğun Proxima'sı (HYG, Hipparcos 2007) gerçeğinden 1.282 AU öndeydi: Gaia'nın yerine taşındı. Roketin durduğu yıldız
  gördüğü yıldız. Ölçek'te de artık orada.

**Roketin gözü** (H0'ın en basit görüntüsü: siyah üstünde yıldızlar):
- 109.400 HYG yıldızı ve Güneş, her biri durgun çerçevedeki yönüyle sapınca uğrar (tan(θ′/2) = e^(−φ) tan(θ/2)).
- Doppler D = γ(1 + β cos θ). Yıldız D·T sıcaklığında kara cisim olarak görünür; akısı D² ile çarpılır.
- Rengi ve görünür payı CIE 1931 2° **resmî tablosundan** (CIE 2019, CC BY-SA 4.0, `tools/olcek_veri.py cie`, sha256
  sabitli): log T üzerinde 10 K - 10¹² K tablo, üstünde Rayleigh-Jeans uzantısı.
- Yıldız dosyası artık sıcaklık taşıyor ("OLS2"; eski "OLST" okunur, sıcaklık renkten geri çözülür).
- Ana işlemcide, trigonometrisiz (yarım açının tanjantı), 64 parçada paralel ve sabit sırayla: her koşu aynı.

**Göz uyumu tasarlanmış:** görüş alanındaki görünür ışıktan (tüm gökyüzü öyle olsaydı), gece göğüne göre yarım uyum,
~1,2 sn'de. Görüntüden okunmaz. Tabanı `hiz.ev = 17,5` (dinginken ~7. kadir görünürlük sınırında), `hiz.uyum = 0,5`.
Karanlık gökte en çok 6 EV açılır.

**Yandan: ölçülen dünya, H0'dan.** Ölçek motoru roketin yanında. Kamera roketin tam yanından bakar: hareket ekranın
sağına, Galaksi'nin kuzeyi yukarı. Dünya roketin çerçevesinde ölçülür: hareket yönünde uzunluklar γ kat kısa, roketin
etrafında.
- Noktaların (yıldızlar, galaksiler, gezegen noktaları) yeri ana işlemcide kısaltılır, ışığı gerçek uzaklığından gelir.
- Işınla çizilenler (küreler, hava, galaksiler) çekirdekte ışın gerdirmesiyle aynı dönüşümü görür (`stretchX`).
- Roket bir nokta. Dünya'nın saatleri durgun çerçevenin zamanından; Galaksi'nin dönüşü tutulur (rota durgun çerçevede).
  Kalkıştan önce bekleyen roket Dünya'yla gider: dünya "bugün"den ilk itişte başlar.
- Sahil, çekirdek, kuşaklar, eve dönüş, tur kapalı.

**Kalıcılık:** proje kökünde `hiz_yolculuk.txt` (git dışı). Her 2 sn'de, varışta, kip değişince ve kapanırken yazılır.
Yazma geçici dosya + ad değiştirme; açılışta bir `.yedek`. NaN diske yazılmaz, sürüm satırı var. Otomatik test kullanıcının
yolculuğuna dokunmaz (`Host::testing`).

### Plandan değişenler (gerekçeleriyle)
1. **Otomatik varış her durumdan en kısa yoldan.** Plan "Space basılınca süzülür, d_dur'da frenler" diyordu; yavaşken
   basılınca yüzyıllarca süzülürdü (φ 0,01'de Proxima'ya ~425 yıl). Şimdi:
   - durabileceği en yüksek hız φ* daha yukarıdaysa hızlanır, tavanda süzülür, durma eğrisinde frenler;
   - zaten o kadar hızlıysan plandaki gibi yalnız süzülüp doğru anda frenler;
   - **W ya da S'ye basmak** kumandayı geri alır; basılı tutmak almaz (W basılıyken Boşluk'a basılabilir);
   - elle hedefin binde biri kadar yakınında durursan son kısmı otomatik varış uçar. Hedefe yalnız fren eğrisinin ucunda
     varılır: ışınlanma yok.
2. **Yandan görünüm H0'da ölçülen dünya.** Plan H0'da en basit hâlini, H2'de ölçülen dünyayı koyuyordu. Durgun
   çerçevede dünya karede c·sinh φ akar (tavanda karede 11,8 ışık yılı): yıldızlar bir karede ekranı geçer, pır pır.
   Ölçülen dünyada en çok c ile akar (planın "%99,9'da karede 5.000 km"si zaten roketin çerçevesiydi).
3. **Göz görüş alanına uyar, bütün gökyüzüne değil.** Kalkışta Güneş burundan 61° uzakta. Bütün göğe uysaydı yıldızlar
   kalkışta kaybolurdu; planın "kalkışta Andromeda soluk bir leke" isteği bunu gerektirir.
4. **Proxima bugünkü yerinde** (4,2457 ışık yılı; plan 2016'nın 4,2465'ini yazıyordu). Kodun ilk hâlinde yanlışlıkla
   SIMBAD'ın J2000 koordinatları vardı; veri denetimi buldu.
5. **Planın fizik metni düzeltildi** (bağımsız kâhin, mpmath 50 basamak):
   - §2.5 Doppler: 1/(γ(1 − β cos θ)) roketin açısıyla yazılır;
   - §2.8 fon ışıması büyük bir disk değil, gittikçe ısınan ve küçülen bir nokta (en çok 0,16°, 1.000 K);
   - §8 D² en çok 2 × 10²¹;
   - §6 Güneş 5.772 K;
   - §3.3 α Erboğa −6,8/−5,5 kadir, 12.060 AU; M31'in perspektifi;
   - §4.5 bedelde kinetik enerji 1,46 saat, foton roketinin yaktığı 2,9 saat.

### Bağımsız inceleme (4 inceleyici, her bulguya ayrı bir şüpheci) ve düzeltmeler
Hepsi düzeltildi:
- **Ölçek'e gerileme:** Planck köpüğü (s < −33,9) kabul edilmiş kareden 3,5 milyon pikselde farklıydı. Tek ifadede iki
  `r.u()` çağrısının sırası C++'ta belirsiz; dosya yeniden derlenince derleyici sırayı değiştirmişti. Üç ifade (köpük,
  çekirdeğin nükleonları, protonun denizi) ayrı satırlara kabul edilmiş karelerin sırasıyla sabitlendi. `readme2`'nin 8
  karesi bit bit aynı. Yer tutucu yıldız ve galaksilerin `v3(rng…)` argüman sıraları gerçek veri yüklüyken görünmüyor;
  sabitlenmedi.
- **Göz:**
  - Dürbün 10⁻⁸ radyanın altına inince uyum konisinin gök payı kosinüs yuvarlamasından 0 oluyordu; 0/0 NaN pozlama, göz
    kalıcı siyah (kayda da geçerdi). Koni artık açıdan ve sinüsten hesaplanıyor.
  - Uyum yakınlaştırmadan bağımsız: noktalar dürbünde parlamıyor, pozlama da değişmiyor (hep 75°'lik alana uyar).
    NaN asla tutulmaz.
- **Yandan:**
  - Hava, Dünya'dan uzakta da kuruluyordu (koşulum yanlıştı); float taşması yarım ekranı karartıyordu.
  - Uzak noktaların derinlik sınamasında |p|² taşıyordu; galaksiler küçük ölçekte kayboluyordu.
  - Top/nokta geçişi kısalmayı saymıyordu; ince şerit sabit örneklerin arasında yanıp sönüyordu.
  - Kuşaklar ilk itişte bir karede kayboluyordu: Hız'da kapalı.
- **Fizik ve panel:**
  - "Bu hızla durulamaz" bildirimi her karede tekrarlanıyordu: bir kez.
  - Dönüş bildirimi hep 153° diyordu (hedefi geçip dönünce gerçek dönüş 27°).
  - Hedefin yakınında S ya da Boşluk ile devralınca yakalama kumandayı geri alıyordu: artık el bir daha itene ya da bölge
    dışına çıkana kadar yakalanmaz.
  - "Dokuz −0,00".
  - "↔" yazı atlasında yoktu.
  - Baş açısı sarılmıyordu (R birkaç tur geri dönüyordu).
  - Bilimsel gösterimde "10,00 × 10⁵" yuvarlaması.
  - Otomatik testin Proxima kareleri dönüşten sonra çekiliyordu.

### Ölçümler (`--test olcek-hiz`, hepsi GEÇTİ)
- Kapalı biçimler / Runge-Kutta (20.000 adım): en büyük göreli fark 1,7e-13.
- 1 − β çift-double hesaba karşı ≤ 2e-16; tavanda 1,000e-21 (21,000000 dokuz).
- Kusursuz sürüş (formülden):
  - Proxima **88,98468 s / 4,24568 yıl**;
  - Andromeda **58,29701 dk / 2,395100 milyon yıl**.
- Otomatik varış, üç kare düzeninde (60 kare/s, 1/300-1/12 sn düzensiz, 0,5 sn): fark ≤ 1,8e-8 s, Dünya saati ≤ 1e-16.
  İki varış da tam hedefte. Fren eğrisine oturtmanın en büyük düzeltmesi ~7 km; o anda roket karede ~10¹⁷ m gidiyor.
- Bağımsız kâhin aynı sayıları buldu: 88,9856082479 s (2016 Proxima'sıyla), 3497,8204 s. Tavan 24,5237170667174523.
- Elle:
  - 46 sn W sonra Boşluk: geçer, durur, döner, tam hedefte durur;
  - 100 sn W'de φ tavanda kalır; S durur, geri gitmez;
  - bir kare W sonra Boşluk: 89,000 sn'de varır;
  - 20 AU kala elle durunca otomatik varış yakalar ve uçar, hiçbir adım hareketin izin verdiğinden uzun değil.
- Sapınç tersi 4e-16. D(θ) = D′(θ′) 7e-16. Trigonometrisiz biçim trigonometrikle 7e-16 (yön 3e-16 rad). Tam arkada girdinin
  1e-16'lık yuvarlaması e^φ kat büyür (koşulsuz nokta, fizikte hiçbir şey).
- Eşyönlü ışımanın enerji yoğunluğu γ²(1 + β²/3)u: 21 dokuzda dahil 1e-10'un altında.
- Planck eğrisi yayımlanan kromatikliklerle (1.000 K: 0,6528 / 0,3445). D³B(ν/D, T) = B(ν, DT): 9e-16.
  Çok sıcakta görünür pay T⁻³.
- Kayıt dosyası bit bit geri gelir, aynı sürer.
- Ölçek'in sınamaları değişmedi: `olcek-hassasiyet` en kötü 9,5e-5 piksel, `olcek-zaman` GEÇTİ, `olcek-sure` kabul
  edilmiş taramayla (`captures/olcek/sure_r2.txt`) sıçrama ve parçacık sayılarında birebir aynı, en uzun kare 15,78 ms
  (bütçe 16; referans 15,71). `readme2`'nin 8 karesi bit bit aynı.

### Kareler ve canlı yol
- Komut satırı: `--headless --hiz --phi 0,2,5,12,24.5237 [--ayak 1 --yer 0.5 --yan 12,16]` → `captures/olcek_hiz/`.
- Göz, yolun ortasında:
  - φ 2'de gökyüzü öne toplanır ve maviye kayar;
  - φ 5'te bütün gök ~20°'lik bir koniye girer;
  - φ 12'de bir nokta;
  - 21 dokuzda pencere kararır (yıldızların görünür ışığı 1/D ile söner); en parlak şey arkadaki Güneş'in öne gelmiş
    görüntüsü.
- Kalkışta hızlanırken Güneş sapınçla öne gelir (φ 2'de burundan 9°), maviye kayar ve gözü kamaştırır (EV 1-3). Güneş
  uzaklaşınca (~20-30 sn) yıldızlar görünür: fizik.
- Yandan, φ 5'te yıldız alanı hareket yönünde 74 kat basılıp dikey bir banda döner; 21 dokuzda bir çizgi.
- Canlı: `--autotest-only olcek/hiz` (pencerede). Hız'ı seçer, 2 × 5 sn W, Tab, tekerlek, sürükleme, R, Boşluk. Proxima'ya
  varır: "senin saatinle 1:38, Dünya'da 4,25 yıl geçti", "Roket Andromeda'ya dönüyor (153°)". Panelde Dünya'da tarih
  **3 Ocak 2031**. Kare 1,3-4,1 ms (pencerede 127-162 fps).

### NE DOĞRULANMADI
- **Özcan'ın gözü:** canlı pencerede bakılmadı; görüntüler komut satırı ve 1280×720 otomatik test kareleri.
- Roketin gözü H0'ın en basit hâli: Güneş ve Proxima yalnız nokta (gerçek diskleri H5). Samanyolu'nun yaygın ışığı,
  galaksiler ve fon ışıması yok (H3-H4). Kalkışta Andromeda "soluk leke" henüz yok.
- Yandan görünümde aşırı γ'da galaksi hacimleri soluyor. Kamera bir galaksinin genişliği kadar yakındayken basık
  galaksinin içinde kalıp "duvar" görüyordu. Ölçülen dünyanın çerçevelemesi H2'nin işi: ortografik bir harita olabilir.
  Noktalar duruyor.
- Pozlama tabanı (17,5) ve uyum payı (0,5) gözle seçildi, ölçülmedi.
- Bir yıldızın görünür akısı V kadirinden ve B−V sıcaklığından, kara cisim varsayımıyla; gerçek tayflar (çizgiler, bant
  farkı) yok.
- Durgun çerçevenin ihmal ettiği hızlar: Proxima'nın 32 km/s'si, M31'in 300 km/s'si (2. ayakta 2.400 ışık yılı = %0,1).
  Roket hedefte durgun çerçevede durur; yörüngeye oturma H5.
- Pencere küçültülünce kareler gelmez: o süre yolculuğa sayılmaz (kare başına en çok 2 sn). Özcan'a sorulacak.
