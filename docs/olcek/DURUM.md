# DURUM · sahne 7 "Ölçek"

> Tek kaynak durum panosu. Plan `docs/olcek/PLAN.md`. En yeni en üstte.

## 2026-10-05 gece · Ö4 ve Ö5'in ilk hâli: kum tanesinden Planck'a bütün iç yol

Akış önce, ayrıntı sonra (Özcan): 10⁻³'ten 10⁻³⁵'e her onlukta gerçek ölçülerle bir resim var, kesintisiz. Cila sırada.

- **Kum ve tane** (10⁻³-10⁻⁶): taneler elipsoit, iki katman; ev tanesi ekranı doldurunca yalnız o çizilir. Tanenin
  üstü Voronoi yüzeyleri, kırağı ve çukurlar (2B gradyan gürültüsü, türevleri analitik).
- **Işığın altında** (10⁻⁶-10⁻⁹): elektron mikroskobu gibi teraslar (kenarlar parlar), sonra atomik kuvvet mikroskobu
  gibi silika çift katmanının petek örgüsü (Si-Si 3,06 Å, köprü oksijenleri). Renk -6 ile -7 arasında söner.
- **Yoğunluk haritası** (10⁻⁹-10⁻¹⁷), kristalografın dili: odaktan geçen düzlemde yoğunluk; her on katta bir çizgi
  (zoom ne olursa olsun bir piksel, sıklaşınca söner), dolgu bu ölçeğin aralığında, log yoğunluk yükseklikmiş gibi
  sol üstten ışıklı kabartma. Tek birim: a0³ başına parçacık. Hepsi gerçek profiller:
  - Si ve O atomları Clementi-Raimondi Slater yörüngeleriyle (kabukların toplamı), silika peteğinin üst katı;
  - Si-28 çekirdeği Woods-Saxon (R 3,07 fm, a 0,52 fm, ρ0 = 0,176 nükleon/fm³: 28 nükleonun zaman ortalaması);
  - proton (rms 0,84 fm) ve üç kuark, Y biçimli gluon akısıyla: derin esnek saçılmanın donmuş anlık görüntüsü
    (sondanın etkileşim süresi partonların ömründen kısa), -13,3 ile -14 arasında belirir.
  - Kuark, sondanın çözünürlüğüyle görülür: protona kadar 0,07 fm, sonra onlukta 1,6 onluk keskinleşir (ölçülen boyutu
    yok, 10⁻¹⁹ m'ye kadar nokta).
- **Pozlama tasarlanmış tablo** (`kDensRange`, s'nin işlevi): ilk denemede dolgunun aralığını ekrandan okudum, çekirdek
  -11'de eklenince bir karede söndü; PLAN §5 zaten yasaklıyordu. Tablo ekranın gerçekte ne tuttuğundan kuruldu
  (`OLCEK_DEBUG` basıyor). Atomun içine girerken aydınlık uç elektronlardan çekirdeğe yarım onlukta geçer: bulut kararır,
  ortada nokta gibi çekirdek kalır. Mesaj bu: atom neredeyse boş.
- **Çöl** (10⁻¹⁷-10⁻³²): kuarkın çevresindeki alanın zaman ortalaması, enerji yoğunluğu u = A / (d² + σ²)². A, o
  uzaklıktaki güçlü, zayıf ve hiperyük bağlaşımları: Standart Model, tek döngü (b = 41/10, -19/6, -7; M_Z'de 1/α =
  59,0 · 29,6 · 8,47; μ = ħc / d). Üçü dilimin üç lobunda (yukarı güçlü, sol alt zayıf, sağ alt hiperyük): halkalar
  -17'de belirgin üç köşeli, derine indikçe yuvarlaklaşır. r⁻⁴ ölçekten bağımsız olduğu için resim her onlukta aynı
  ("yeni bir şey yok"), değişen yalnız bağlaşımlar. Gerçek sayılar: kesişmiyorlar, yaklaşıyorlar; Planck'ta yeniden
  ayrılıyorlar.
- **Dip** (10⁻³²-10⁻³⁵): Wheeler köpüğü: her uzaklık ~ Planck uzunluğu kadar belirsiz. Oktav başına yer değiştirme
  0,4 L_P (mertebe); her oktav kendi ışık geçiş süresiyle yaşar. Ekranda 0,15 sn'lik deklanşörden hızlı olanlar
  ortalamada söner, yavaşlar oynar. Kendiliğinden yalnız son ~1,5 onlukta görünür. Kuark kapalı bir sicim: halka
  (yarıçap 0,32 L_P), 2-5. modlar n c / r ile döner. Deklanşörün keskin tuttuğu kısım çizgide, ortalanan salınım
  genişlik olur (dünya yüzeyi). Uzaklık halkaya ölçülür, iç halkalar sicimi sarar.
- **Plandan ayrılanlar** (gerekçeleriyle):
  - *Çizgiler:* PLAN §5 "görüntü üstünde çizgi yok" diyor; yoğunluk haritasının eş-değer çizgileri ölçeği gören aletin
    kendi çizimi (kristalografi haritaları), etiket ya da ok değil. Sönük tutuldu.
  - *Çölde pus yerine alan:* plan dalgalanmaları ortalanmış pus olarak düşünmüştü; ortalaması gerçekten tekdüze. Gözle
    görülür ve dürüst olan, kuarkın kendi alanının durağan ortalaması. Kıpraşma yok, deterministik.
  - *Loblar:* resmin bir aracı (üç kuvvet aynı yerde üst üste; ayrı loplar yalnız karşılaştırmayı görünür kılıyor).
  - *Sicimin boyu:* kuram bir mertebe veriyor, sayı vermiyor; dipte kadraja sığsın diye 0,32 L_P seçildi.
- Eski yer tutucu pus (`GHaze`) kaldırıldı. Hızlı oktavları %25 kontrastla hareket ettirdiği için pır pır üretirdi.

### Ölçümler (2560×1440)
- Kare: iç dünyada en kötü 8,8 ms (atomik görüntü); yoğunluk haritası 1-2 ms, çöl 0,7 ms, dip 2-3,7 ms.
- `olcek-sure` (bütün yol, adım 0,01): en uzun kare **15,2 ms, bütçe GEÇTİ**; ev kadrajda en kötü 0,59. Sıçrama 173
  kare (eşik 0,04):
  - 114'ü s 23,4-23,7: galaksi geçişi, eskiden beri;
  - 23'ü s ≈ 6-7: köşeler, karelere bakılmıştı;
  - 12'si s 16-17: yıldızlar;
  - iç yolda yalnız s -16/-17'de 21 kare (en büyük 0,045): kuarkın keskinleşmesi ve alana devir. İlk denemede 127
    kareydi; keskinleşme onlukta 2'den 1,6 onluğa indi ve alanın aydınlık ucu sondanın yayılmasını izledi;
  - dipte 3 kare (0,065): köpüğün gerçek hareketi.

  -9 ile -15 arası sıfır.
- `olcek-hassasiyet`: en kötü 9,5e-5 piksel, GEÇTİ. `olcek-zaman`: GEÇTİ.
- Sıçrama raporu artık onluk başına sayı ve en büyük değeri basıyor (eskiden yalnız en büyük 40 sıçrama).

- **Dibin cilası (aynı gece):**
  - *Sicim artık kendisi çiziliyor.* Deklanşörün ortaladığı salınım, bir sinüsün konumlarının arksinüs dağılımı:
    dönüş noktalarında parlak, ortası sönük. Kendi genişliğiyle Lorentz eğrisi olarak genişletildi; kapalı biçimi
    (1/π) Re 1/√(A² - (δ + iγ)²). Durgun sicim ince bir çizgi, hızlısı kenarları parlak bir bant (uzun pozlamada
    titreşen tel gibi).
  - *Köpüğün hücreleri.* Planck boyunda Voronoi hücreleri, duvarları bir bükülmeyle eğri (köpük, tabak değil);
    duvarlar üçlü noktalarda buluşuyor. Her hücre uzaklıkları kendi kaymasıyla kaydırır, halkalar duvarda kırılır.
    Hücreler bir Planck zamanı yaşar, deklanşörden hızlıyken ortalamada söner: yalnız son ~0,3 onlukta görünür.
  - İlk denemede duvarlar düz ve merkezden kesilmiş pasta dilimleri gibiydi (`captures/olcek/o5_d`); bükülme ve
    piksel başına bir kez hücre araması (komşular aynı kaymayı alır) ile hem doğal hem 8 ms'den 5 ms'ye.

### NE DOĞRULANMADI
- Canlıda (tekerlekle) hiçbir şeye bakılmadı; yalnız kareler ve tarama.
- Köpüğün hücreleri ekranda ancak 2-4 tane (dipte ekran 1 Planck uzunluğu); hareketleri canlıda görülmedi.
- Bağlaşımlar M_Z'nin altında da tek döngü SM b'leriyle (2 GeV'de α3 ≈ 0,24, gerçeği ~0,30); temsil için yeterli,
  ölçülmedi.
- Yoğunluk haritasının tonları ve atomun içindeki pozlama geçişi gözle ayarlandı; bir ölçüte bağlanmadı.
- s ≈ 23,4-23,7'deki galaksi geçişi sıçramaları hâlâ duruyor (eskiden beri).
- autotest bu değişikliklerle koşulmadı.

## 2026-10-05 akşam · Kozmik ağ: hacim ve dilim

- **Ağ** (`tools/olcek_veri.py ag`): CDM benzeri tayflı (BBKS, Γ = 0,2) Gauss rastgele alan, Zel'dovich yaklaşımıyla
  taşınan parçacıklar → duvarlar, iplikler, düğümler, boşluklar; 256³ hücre, 600 Mpc'lik periyodik kutu (2,3 Mpc),
  boşluklardaki ızgara izi 0,8 hücrelik Gauss'la silindi. Kart belleğinde mip'li 3B alan; iki okuma (600 ve 1530 Mpc,
  döndürülmüş) kutunun tekrarını gizler. Gerçek bölgenin haritası değil, gerçek istatistiğin bir örneği (PLAN §10).
- **Işık**: galaksilerin ortalama ışık yoğunluğu (10⁸ L_güneş/Mpc³) × alan^1,6 (galaksiler maddeden kümeli), kozmolojik
  (1 + z)⁻⁴ sönümü (ΛCDM, H0 70, Ωm 0,3; z bizden uzaklıktan). 2MRS'nin ulaştığı yerde noktalar ışıktır; anket akıyla
  sınırlı olduğundan kaçırdığı ışık uzaklıkla artar, ağ yalnız onu doldurur (1 - e^-(r/140 Mpc)²).
- **Plandan ayrılış: dilim.** Gigaparseklerce derinlik üst üste binince ağ düz bir sise dönüşüyordu (iki deneme:
  `captures/olcek/o3_f`, `o3_g`; Olbers paradoksunun kısa hâli; bu yüzden her anket ve simülasyon görseli bir dilimdir).
  Bu ölçeğin gözü, odaktan geçen ve bakışa dik ~40 Mpc'lik bir dilimi görür (Gauss, σ 20 Mpc); galaksi noktaları da
  aynı dilimle. Odak biz olunca bizim içinde durduğumuz dilim. "Işık olan yerde gerçek ışık" kuralından bilerek sapma;
  gerekçe görünürlük: aksi hâlde 10^25-10^27'de yapı yok.
- Sonuç (2560×1440): 10^25'te 2MRS'nin gerçek dilimi (büyük duvarlar, Samanyolu düzleminin gizlediği boş şerit),
  10^25,5-10^26'da gerçek anket istatistiksel ağın içinde, 10^27'de kenarında ilk ışık kabuğuyla küre. Kare ≤ 5 ms.
- Pozlama 10^25,6 üstünde sabit (EV +36): dilimin yüzey parlaklığı ölçekten bağımsız.

## 2026-10-05 akşamüstü (2) · Samanyolu, gerçek yıldızlar ve galaksiler, kumsal

Planın sırası (Ö2 → Ö3) Özcan'ın *"harbiden Samanyolu gibi"* sözüyle karıştırıldı: önce en çok göze batan yer tutucular.

- **Samanyolu** (Ö3'ün ilk parçası): yüzden bir model, ölçümlerden. Kollar Reid ve ark. 2019 (ApJ 885, 131) Tablo 2'nin
  kırıklı log-periyodik sarmalları, makaleden birebir (3 kpc, Norma, Sct-Cen, Sgr-Car, Yerel, Perseus, Dış); dört ana kol
  ölçülmeyen tarafta 12° eğimle bir sarmal sürer (temsil), kısa kollar ölçüldükleri yerde kalır. Kol genişliği makalenin
  w(R) = 336 + 36 (R - 8,15) pc'si. Uzun çubuk Wegg ve ark. 2015 (yarı boy 5 kpc, Güneş-merkez çizgisiyle 28°). Üstel eski
  disk (2,6 kpc), çubuk ve çekirdek, kollarda genç yıldız bulutları ve H-alfa düğümleri, kolların iç kenarında toz
  şeritleri (kızartarak), ince oktavlar iki pikselden küçük olunca söner; piksel başına bir kez (2-5 ms). Güneş Yerel
  kolun hemen içinde, Sgr-Car ile Perseus arasında (ölçülen yeri). İlk iki deneme plak gibi halka halkaydı (kolları
  birkaç tur uzatmıştım), sonra pürüzsüz boru gibiydi: kareler `captures/olcek/o3_a..d`.
- **Yıldızlar** (Ö2'nin ilk parçası): yer tutucu alan yerine HYG v4.1'in uzaklığı bilinen **109.400 yıldızı** (gerçek
  yer, mutlak kadirden ışık, B-V'den renk). 10 pc içinde 318 yıldız.
- **Galaksiler**: 2MASS Redshift Survey'in **43.490 galaksisi** (Huchra ve ark. 2012; uzaklık kırmızıya kaymadan,
  H0 = 70, ışık K bandından). 10^25-10^25,5'te yakın evrenin gerçek ipliksi yapısı görünüyor. **Açık sorun:** anket
  200 Mpc'yi kapsıyor, ötesindeki yer tutucu ağ 170 bin kat seyrek; 10^26-10^27'de çevremiz parlak bir top gibi. Doğrusu
  ağı hacim olarak çizmek (ortalama ışık yoğunluğu ~10^8 L_güneş/Mpc³), Ö3'ün sıradaki işi.
- **Kumsal** (s 0-1): tane dokusu piksel ölçeğine kadar (3,5 pikselden ince oktav söner), su kenarı santimlerde
  düzensiz, köpük gürültü sırtlarından dantel. **Doku süzgeci tuzağı:** ekran kartının çift doğrusal süzgecinin
  ağırlıkları 8 bit; 2 m'lik kıyı alanı 1 m'lik ekranda 8 mm'lik basamaklar çiziyordu. Kıyı alanı artık nokta örnekle
  okunup float'la ara değerleniyor.
- Bulutlar: Çandarlı'nın tuzlası yanlışlıkla bulut sayılıyordu (parlak ve beyaz); o gün sahilin üstü açık olduğu için
  15 km çevre bulutsuz.
- `--autotest-only olcek`: 72-162 fps (en yavaş 1 km'de 13,5 ms ve atomda 12,9 ms), kart belleği 4,2 GB.
- `olcek-sure` (Ö1 içeriğiyle, galaksiden önce): kare her yerde ≤ 15,7 ms, **bütçe geçti**; ev kadrajda (en kötü 1,00
  s = 24,8'de: Laniakea geçişi 24,6-25,4'e kaydırıldı). Sıçramalar 120 kare: s ≈ -10,5 (zemin sönerken arkasındaki
  bulutları örtüyordu: düzeltildi), s ≈ 7 köşeler (karelere bakıldı: odak düzlemi yerden Dünya'nın merkezine kayarken
  kenarın akışı, gerçek hareket; ölçünün derinlik varsayımı), s ≈ 26 (kozmik ağın yer tutucusu).

### NE DOĞRULANMADI (bu parça)
- Sıçrama ölçüsü galaksi, gerçek yıldızlar ve 2MRS ile yeniden koşulmadı.
- Samanyolu'nun ölçülmeyen yarısı ve kol uzantıları temsil; renkler ve parlaklıklar gözle ayarlandı, bir fotoğrafa karşı
  ölçülmedi. M31 ve M33 hâlâ eski düz disk yer tutucuları.
- Canlıda elle hiçbir şeye bakılmadı (yalnız autotest'in sanal tekerleği).

## 2026-10-05 akşamüstü · Ö1 sürüyor: gerçek Dünya, Çandarlı, bugünün bulutları, hava

Özcan yer tutucuları görünce *"harbiden Dünya gibi"* bekliyordu (Universe Sandbox seviyesi); ev katmanı artık gerçek
veriden. Sahil **Çandarlı**'ya taşındı (Özcan: *"kale var ve onun önünde sahil var. O sahili kullanabilir misin"*).

### Ne yapıldı
- **Sahildeki nokta**: Çandarlı Kalesi'nin (38,9338 K, 26,9337 D; Wikipedia, UNESCO geçici listesi) ~110 m batısında, batı
  koyuna bakan kumsalda, Sentinel-2 su maskesinin sıfır çizgisinde (38,93388 K, 26,93247 D). Güneş öğlesi 12:59:18 TSİ.
  Panelde 10³-10⁴ m "Çandarlı Körfezi".
- **Haritalar** (`tools/olcek_veri.py`, `.tex`: mip'leri hazır RGBA8 sRGB, açılışta çözme yok; 2,1 GB kart belleği):
  Blue Marble NG Ekim (dünya 21600×10800, Ege 500 m yerel ızgarada 2048 km), Sentinel-2 L2A (2025-09-23, dört karo, 10 m,
  82 km; TCI doğrusal yansıtma olarak okunur: DN 255 = 0,2), Ay (LRO renk 8K), Blue Marble'ın enlem ortalaması (dönen
  Dünya'nın kuşağı). Yerel haritalar sahildeki noktanın doğu/kuzey metresinde, kenarlarının onda birinde söner.
- **Kıyı**: Sentinel-2'den su maskesi → denizin işaretli uzaklığı (4 km kare, 2 m; `kiyi.sdf`). Yakın plandaki kumsal,
  dalganın son ucu ve köpük bu gerçek kıyı çizgisine oturur; kumsalın arkası ve açık deniz uydudan.
- **Bugünün bulutları** (NASA GIBS): ev çevresinde **MODIS Terra 5 Ekim 2026** (geçişi ~09:00 UTC, sahilde öğleden bir
  saat önce), dünya için VIIRS NOAA-20 (bugünün günü henüz tamamlanmadı: %74 boş; şimdilik 4 Ekim). Bulut = uydunun
  bulutsuz Blue Marble'dan parlak ve beyaz gördüğü yer. Çandarlı'nın 15 km çevresi bulutsuz tutuldu: o gün gökyüzü açık,
  farkın bulut sandığı tuzla. Bulutlar 10^3,7-10^4,3 m arasında incelir (kamera onların yüksekliğine iner).
- **Hava**: tek saçılım (Rayleigh, aerosol, ozon), Chapman (Schüler) ile güneş ışığının yolu, sabit adım (20, ışının en
  alçak noktasına doğru sık), piksel başına bir kez. Açık gün: aerosol optik derinliği ~0,03. Yer ışığı da havadan
  geçerek ve gökten aydınlanır.
- **Dünya'nın dönüşü** (plandan ayrılış): kamera 10^7 m'ye kadar yerle döner, 10^7-10^8,6 arasında bırakır (önce 6,6-7,8);
  dönüşün göz kapağı **bir kare** (1/60 sn). Neden: planın zaman yasasıyla Dünya 10^7-10^8'de saniyede 1-3 tur atıyor,
  0,15 sn'lik kapak onu Jüpiter gibi kuşaklara ortalıyordu (karede görüldü); şimdi küre önce yavaşça dönerken görünür,
  küçüldükçe hızlanır. Yörüngelerin kapağı 0,15 sn kaldı (halkalar).
- Yakın plandaki dokular gradyan (Perlin) gürültüsüne geçti: değer gürültüsünün kafes köşeleri köpüğü kareli gösteriyordu.

### Ölçümler (2560×1440)
- `olcek-hassasiyet`: en kötü 9,5 × 10⁻⁵ piksel, GEÇTİ. `olcek-zaman`: varışta sapma 0; geri çekilirken Dünya kendi
  hızının en çok 1,0 (0,5 onluk/sn) · 1,9 (2) · 8,4 (8) · 21 (40) katı döner (önce 1,1 · 2,6 · 10 · 63).
- Kare: sahil (s 0-1) 5 ms, s 2-3 10-12 ms (kumsal + uydu + hava), s 3,5-7 ~3 ms, uzay < 1 ms.

### NE DOĞRULANMADI (Ö1, şimdiye kadar)
- Süreklilik sınaması (`olcek-sure`) yeni içerikle koşulmadı. Canlıda elle bakılmadı.
- Sentinel-2 ile Blue Marble arasında renk eşlemesi yapılmadı (kenar sönümü var, ton farkı s ≈ 5'te görülebilir).
- s 2,5-3,5 en zayıf yer: 10 m'lik uydu 4-25 kat büyütülmüş (bulanık), kumsalın bandı üstüne biniyor.

## 2026-10-05 öğleden sonra · Ö0 bitti: iskelet ve ölçek motoru

Galeride **07 Ölçek** (vurgu rengi gül `#E08BAE`, geçici galeri resmi evrenin kenarından). 62,0 onluğun hepsi
kaydırılabiliyor: `s` = log10(ekranın genişliği, m), alt uç Planck uzunluğu (s = -34,79), üst uç gözlemlenebilir evrenin
ekranın yüksekliğine sığdığı yer (s = 27,25). Cisimler **yer tutucu** (Özcan sordu: *"tamamen temsili mi olacak"*;
cevap: dışarısı Ö1-Ö3'te gerçek veriyle gerçek görünecek, ışığın dalga boyunun altı ister istemez temsili).

### Ne yapıldı
- **"Bugün"** (`tools/olcek_bugun.py`, astropy 7 + JPL DE440s → `src/scenes/olcek/olcek_bugun.h`): sahildeki noktada
  gerçek güneş öğlesi **12:58:56 TSİ** (09:58:56 UTC). Gezegenler, Ay, Güneş (Dünya'dan), Dünya'nın dönüş matrisi,
  Samanyolu'nun kutbu ve merkezi o anın gerçek yerinde. Planın iddiaları ölçüldü: başucu galaktik kuzey kutba **11,6°**
  (plan ~11°), gezegenlerin düzlemi **51,0°** eğik (plan ~50°), Güneş'in yüksekliği 46,8°.
- **Çapa ağacı** (`olcek_world.*`): Laniakea → Yerel Grup → Galaksi merkezi → Güneş → Dünya → bugünkü sahil noktası →
  sahil noktası → çekirdek → proton → kuark, yanlarda Ay, gezegenler, M31, M33, Macellan bulutları. İki çapanın arası
  yalnız ortak atalarına kadar toplanır. Kamera her an odağın çapasına göre tutulur, GPU'ya D (odak uzaklığı) biriminde
  float gider.
- **Odağın yolu** (PLAN §3): kuark ↔ proton ↔ çekirdek ↔ sahil ↔ Dünya ↔ Güneş ↔ Galaksi merkezi ↔ Yerel Grup ↔ Laniakea
  ↔ biz; her geçiş W'ye doğrusal karışımla (ekran düzgün hızla kayar). Kamera yalnız başucu boyunca uzaklaşır; yön
  yıldızlara göre sabit (kuzey yukarı, doğu sağ).
- **Zaman** (PLAN §4): R(s) = W^0,68 (W ≥ 1 m), W^1,24 (W ≤ 1 m); katman saatleri (Dünya'nın dönüşü ve yılı, Ay,
  gezegenler, galaksinin dönüşü ve kolları, örgü titreşimi, çölün oktavları). **Gözün kapağı**: Gauss pozlama
  (σ = 0,15 sn); hızlı yörünge halkaya döner (ışığı korunarak, örnekler zaman eşit aralıklı: yavaş yerde parlak), hızlı
  dönen Dünya'nın deseni boylamda ortalanır, örgü titreşimi analitik olarak (her kipin genliği e^(-ω²σ²/2) kadar kalır).
- **Eve dönüş**: kamera 10^6,6 m'nin altında yerle birlikte döner (sahil hep öğle, yer kaymaz); oradan 10^7,8'e kadar
  Dünya kendi hızına kavuşur. Dönerken yakınlaşmayla orantılı olarak bugünkü dönüşe çekilir (kısa yoldan). Yıl için aynısı
  10^11,9-10^12,9 arasında: odak Dünya'dan Güneş'e geçmeden Dünya yörüngede yürümez (yoksa kamera yörüngeyle birlikte
  bütün görüntüyü savururdu).
- **Çizici** (`olcek_kernels.cu`): piksel başına 4 sabit örnek (R2, ilki merkez, Blackman-Harris), birikim yok, rastgele
  sayı yok. Küre kesişimi kameranın yüksekliğinden (çift hassasiyetle hesaplanan) kararlı formülle: ekranın 10^12 katı
  bir küre yüzeyinin hemen üstünden de doğru. Noktalar ekranda Gauss lekesi, toplamları akıları: küçülen cisim ışığını
  koruyarak noktaya döner (1-2,5 piksel arası ikisi birlikte). Bulutlar (Gauss) analitik sütun, galaksi diskleri analitik.
  Rengin bittiği eşik: 10^-6 → 10^-7 m arasında renk söner.
- **Panel**: "Bakılan" (PLAN §2'nin tablosu, iki dil) ve "Ekranın genişliği" (fm, pm, nm, µm, mm, cm, m, km, milyon /
  milyar km, ışık yılı, bin / milyon / milyar ışık yılı; 10^-15 m'nin altında 10'un kuvveti). Yazı atlasına üst simgeler
  eklendi; IBM Plex'te üst simge eksi (⁻) yok: eksi işaretinden üst simge boyunda çiziliyor (`src/ui/ui.cpp`).
- **Tuşlar**: tekerlek (tık ×1,5, en hızlı saniyede 20 onluk), R sahile dön, T tur (evrenin kenarı, sahil, Planck,
  sahil; saniyede yarım onluk). `--autotest-only olcek` bütün yolu dolaşıyor.

### Ölçümler
- `--test olcek-hassasiyet` (her 0,02 onlukta görünen çapalar ve yanlarındaki bir nokta; float ile double-double):
  **en kötü 1,1 × 10⁻⁴ piksel**, 27.855 ölçüm, GEÇTİ (eşik 0,01). Sınama iki hata yakaladı: (1) çekirdeğin titreşimi
  zaman oranına bölünüyordu (çarpılmalıydı), titreşim hep sıfırdı; (2) bugünkü sahil noktası Dünya'nın merkezine
  bağlıydı, 10^-10 m'de proton 6.400 km'lik toplamın içinde kayboluyordu (0,014 px; titreşim açılınca ekranlarca olurdu).
  Şimdi sahil noktası bugünkü noktaya asılı.
- `--test olcek-zaman` (140 yolculuk: 7 uzaklığa çık, 5 bekleme, 4 dönüş hızı): sahile varışta **Dünya'nın dönüşü 0 rad,
  yılı 0 sn, sahil noktası 0 m** sapma, GEÇTİ. Bedeli: dönüş bölgesinde Dünya kendi hızından hızlı döner: saniyede 0,5
  onlukla inerken en çok **1,1 kat**, 2 onlukla 2,6, 8 onlukla 10, 40 onlukla 63 kat (planın ≤ %30'u yavaş inişte tuttu,
  hızlı inişte tutmuyor; Özcan canlıda görüp karar verecek).
- `--test olcek-sure` (üst uçtan alta ve geri, 0,01 onluk adımla 12.408 kare, zaman donuk): kare **her yerde ≤ 6,5 ms**,
  bir yer hariç: s 6-7'de 23,7 ms (yer tutucu Dünya deseninin dönüş ortalaması: 12 dokunuşta 6 oktav gürültü; Ö1'de gerçek
  dokuyla gidecek). Ana işlemci ≤ 2 ms. Sıçrama ölçüsü (bir kare, öncekinin yakınlaşma kadar büyütülmüşüyle; 2 piksel
  hoşgörü, kenar blokları hariç, eşik 0,04): ilk koşu 285 kare → düzeltmelerden sonra 127 kare. Düzeltilenler:
  - **s ≈ 2,9 (0,36):** Dünya'yı merkezinden ortalama yarıçaplı bir küre çiziyordum; sahil noktası elipsoidin üstünde,
    yerin merkezine 6.369,9 km, düşeyi merkezden 0,19° sapar: kamera 1 km yükseklikte kürenin yüzeyinin altına
    düşüyordu. Dünya küresi artık sahil noktasından geçer, merkezi yerel düşey üstünde.
  - **s ≈ -10,3 (0,18):** 0,25 mm'lik kum tanesinin küre kesişimi 0,1 nm'den bakınca float'ta bütün basamaklarını
    kaybediyordu (atom bir görünüp bir kayboluyordu). Ev tanesi için kesişim kameranın yüksekliğinden.
  - **s ≈ -10,5:** zemin atomun içine geçerken birden kalkıyordu; artık bir onlukta söner.
  - Ev kadrajda: Yerel Grup geçişinde Güneş kenara taşıyordu (1,08); geçiş 22,0-23,0'a kaydırıldı.
  - Kalanlar (yer tutucuya ait): s ≈ 7 köşelerde Dünya deseninin dönüş ortalamasında dokunuş sayısının tam sayı adımları
    (Ö1); s ≈ 26-26,6 kameranın kozmik ağın içinden geçerken yakınındaki galaksiler (yakın sönümü eklendi, Ö3'te ağ
    yeniden); s ≈ 11,5 Güneş'in alt kenarda derinlik farkı (ölçünün odak düzlemi varsayımı, sıçrama değil).
- `--autotest-only olcek`: 162 fps (ekranın 165 Hz'i), Dünya'da 40 fps (yer tutucu desen), kart belleği 2,2 GB.

### Plandan ayrılanlar (gerekçeleriyle)
- **Görüş açısı yatay 40°** (planda sayı yoktu): sahilde 1,4 m'den 1 m'lik bakış; evren dikeyde sığsın diye üst uç 27,25.
- **Yılın serbest bırakılması 10^11,9'dan sonra** (plan "s 8 → 5 arası dönme ve yörünge çekilir" diyordu): odak Dünya'dan
  Güneş'e geçerken Dünya yörüngede yürürse kamera onu izler, Güneş saniyede bir tur atar. 10^11-10^11,9 arasında Dünya
  yörüngede durur (o ölçekte bir nokta), öteki gezegenler yürür.
- **Pozlama eğrisi** (`scenes/olcek.cfg`, `ev`): gezegenlerin halkaları ve yıldızlar uzun pozlamayla görünür (s 13,5-22,6
  EV +23), galaksiler nokta olunca onlukta ~6,6 EV açılır (ışıkları uzaklığın karesiyle söner). Ö2-Ö3'te gerçek
  içerikle yeniden ayarlanacak.
- **Sahil noktası** Özcan'ın isteğiyle **Çandarlı**'ya (Dikili; kalenin önündeki kumsal) taşınıyor: Ö1'in ilk işi.

### NE DOĞRULANMADI
- Canlı pencerede elle tekerlek: yalnız `--autotest` betiğinin sanal tekerleği çevirdi; hızlı çevirmenin hissi, R'nin ve
  turun akışı gözle izlenmedi. Özcan eve gelince.
- Gözün kapağının (σ = 0,15 sn) halkaları ve Dünya'nın dönüş ortalamasını hareket hâlinde gözle görmedim; yalnız tek
  kareler. Ay'ın ve gezegenlerin halkaya dönüşü kare olarak var, akışı canlıda bakılmadı.
- Kararlılık (`tools/kararlilik.py`) bu sahnede koşulmadı: sabit örnek deseni ve birikimsizlik Heykel'inkiyle aynı, ama
  ölçülmedi.
- Yıldızlar, galaksiler, kozmik ağ, atom altı: hepsi yer tutucu; boyutlar ve yerler gerçek, sayıları ve görünüşleri değil.

### Sıradaki: Ö1 · Ev
Veri hazır (`tools/olcek_veri.py`, `assets/olcek/KAYNAKLAR.md`): Sentinel-2 (2025-09-23, 10 m, 82 km), Blue Marble NG Ekim
(dünya 21600×10800 ve Ege 500 m), Ay (LRO). Bugünün gerçek bulutları için NASA GIBS VIIRS (5 Ekim 2026; Avrupa geçişi
akşama tamamlanır). Sıra: sahil noktasını Çandarlı'ya taşı → dokulu Dünya ve Ay → bulutlar → atmosfer → sahil ve kum
gerçek kıyı çizgisine oturur.

## 2026-10-05 gecesi · Plan yazıldı

Özcan'la uzun bir konuşmada karar verildi: saf adres (Powers of Ten gibi), İzmir'de bir sahilden dışarı gözlemlenebilir
evrene, içeri Planck uzunluğuna, ~61,7 onluk, yalnız tekerlekle, kesintisiz. Zaman akıyormuş gibi akar (dışarı onlukta
×4,8, içeri ÷17), etkisi yok, sahil hep öğle. Görüntüde yazı yok; panelde bakılan cismin adı ve ekranın genişliği.
Sıradaki: **Ö0 · iskelet ve ölçek motoru** (bütün onluklar yer tutucularla, süreklilik ve hassasiyet sınamaları).
