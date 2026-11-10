# DURUM · sahne 3 "Yıkım"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/yikim/PLAN.md`'de.

## Şu an
- **2026-11-07 · Yıkım performansı.** Kutuların yüz ve kenar temasları analitik
  hesaplanıyor. Yıkım topunun teması küre yüzeyinden geliyor. Kırılan bağlar
  iş listesinden düşüyor ve bağımsız bağ hesapları birbirini beklemiyor.
  Fizik saati kare başına tek adıma bağlı olmadan, saniyede 2.400 alt adımla
  ilerliyor. Hasar rengi ve görüntünün OpenGL'e aktarımı GPU'da kalıyor.
  [Temas çokgeni yöntemi, GDC 2007](https://box2d.org/files/ErinCatto_ContactManifolds_GDC2007.pdf) ·
  [CUDA koşullu grafik döngüleri](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html#conditional-graph-nodes)

  **Doğrulama · hafif.** Release derlemesi ve `--scene yikim --test` ile
  34/36. Başlangıçta da kalan iki konsol kontrolü aynı sayıları verdi.
  `--scene yikim --test temas` çapraz iki ince çubukta 2,0495 m ölçtü.
  Kutu teması kapatılınca çubuk 1,0927 m'ye düştü ve test kaldı. Sabit
  pozda, aynı kamerayla üç görüntü modunun 1080p PNG'leri eski sürümle
  birebir eşleşti.

  **Kapanış.** Özcan, *"Bu benim için yeterli bir seviye. Test edebilirim."*
  En ağır yüklerdeki anlık kare ve fizik saati farkı ertelendi (deferred).
  Denemek için `build\pc_graphics.exe --scene yikim`.

- **🏁 2026-10-02 (gece) · Sahne tamamlandı.** Özcan: *"Tamamdır. Yıkım sahnemiz tamamlandı. Yeni bir sahne tasarımına
  geçeceğiz ama onu artık yeni oturumda yapalım."* Açık kalanlar (istenirse): gökdelenin göçüşünde 24. sn sonrası ikinci
  bir belirlenimsizlik kaynağı; sismik kulede arayüz geçişinin gökdelenden uzun sürmesi; evin ayakta 19-21 ms'si.
- **2026-10-02 (gece, kapanış) · Kamera baktığı yöne uçar.** Özcan: *"w ya basınca düz gitmek yerine kemeranın yönüne
  göre ileri gidebilir"*. W/S artık bakış yönünde (yukarı bakınca yükselir, aşağı bakınca alçalır); A/D yatay, E/Q
  dikey kaldı; yerden 0,3 m'nin altına inmez. Yeni test `kontrol: yukari bakip W, kamera yukselir` (kontrol 5/5;
  bütün testler 33/35, aynı iki konsol).
- **2026-10-02 (gece, en en son) · Dördüncü yapı: sismik izolasyonlu kule.** Özcan: *"Japonların binaları var ya hani.
  Sallandıkça sallanıyor ama yıkılmıyor. [...] Bu 4.bina da gökdelen olacak ama işte bu sallanma özellikli olacak.
  Şekli, estetiği de daha modern olursa daha güzel olur."* Kararları: *"yalnız izolasyon, yansımalı cam, tabanı geniş
  yukarı doğru incelen o en ucunda iğne gibi hale gelen gökdelenlerden"*, hedef *"9 Depremine zar zor dayanması [...]
  8 de minimal yıkılma. 7 de neredeyse hasar yok"*, *"aşırı uğraşmana gerek yok"*.
  - **Yapı** (`buildSpireTower`, `yapi = sismik`, panelde "Sismik"): üç kademe (5×5, 3×3, 1×1 açıklık; 10 + 10 + 6
    kat, her biri alttakinin ortasında) ve 27 m'lik çelik iğne, toplam ~123 m; 5.142 parça. Her açıklıkta tek büyük cam
    panel, koyu antrasit çerçeve. 1 m'lik beton radye zemine yapışık değil, **kauçuk izolatörlerin** üstünde:
    yanlamasına yumuşak (kule onların üstünde 3,5 sn periyotla salınır; rijitlik toplam kütleden hesaplanır), düşeyde
    sert, hiç kırılmaz (malzemeye `G` kesme modülü eklendi: fizikteki tek değişiklik). Radyenin çevresinde 0,9 m'lik
    hendek, zemine yapışık bir bordürle: zemin onunla birlikte sallanır, kule süzülür. Beton 40 MPa, donatı 420 MPa
    (yeni bir kule; öteki yapıların 16 / 220'si 26 katın altında kolon ayağında eziliyordu).
  - **Cam:** çizicide yeni malzeme: koyu bir renk üstünde ayna yansıması, payı Schlick Fresnel'iyle (kaplamalı cam,
    F0 0,25); yansıma bir kez izlenir: başka bir parça ya da zemin her zamanki gibi aydınlatılır, yoksa gök ve güneş.
  - **Ayarlama** (ölçerek): izolasyon 3 sn, sıradan beton: 8 ve 9'da taban katları ezildi, kule çöktü (80 bin kırık
    lif); yastık en çok 0,5 m kaydı, bordüre çarpmadı. Yüksek dayanımlı malzeme ve 3,5 sn ile 8 ve 9'da ayakta, ama
    iğne 8'de kırbaç gibi kopup düştü; içi boş (1.200 kg/m³) ve ayağı sağlam (%6 donatı) iğne yerinde kaldı.
  - **Ölçüldü** (sarsıntı bittikten ~8 sn sonra; öteleme yastıktaki kaymayla birlikte):

    | Büyüklük | Kırık lif (105 bin) | Sonuç |
    | --- | --- | --- |
    | 5 | 0 | hasarsız |
    | 6 | 0 | hasarsız, öteleme 8 cm |
    | 7 | 324 | neredeyse hasarsız, öteleme ~1 m |
    | 8 | 5.478 | birkaç cam panel, iğne yerinde, tepe 6 cm iner |
    | 9 | 17.219 | ayakta: kademelerde ve zemin katta boşluklar, düşen cam ve parçalar; tepe 12 cm iner |

  - **Pencerede** (`--autotest`): ayakta 46 fps, 8'in ortasında 41 fps (fizik 22 ms; dünya gerçeğin ~%74'ü). Fizik
    ayakta ~17,5 ms: arayüz geçişi gökdelenden uzun (profilde 14,8 / 11,0 ms) ama renk sayısı değil (10 / 9 renk);
    sebebi bulunmadı, açık.
  - Tanı: `--set test.iso=1` (her saniye tabanın zemine göre kayması); klipteki "dunya:" satırı renk sayısını da yazar.
    `--autotest` sismik kuleyi 8'de sallar.
- **2026-10-02 (gece, en son) · Topun en güçlüsü artık bir göktaşı.** Özcan: *"topun max gücünü çok arttırabilelim.
  Eğlence amaçlı. Son hali cidden abartı olsun. Gökdelen sağlam olduğu için şuanki en güçlü zarar veriyor ama
  yıkamıyor."*
  - Çubuğun sağ ucu 0,7 m · 11,4 t · 70 m/s (mermi) yerine **2,5 m · 520 t · 110 m/s** (saatte 396 km; çubukta adı
    "Göktaşı"). Hız fizikteki 120 m/s korumasının altında kalsın diye abartı boydan ve kütleden. Sol yarı ve çentik
    aynı. Hazır toplara yedi boy eklendi (0,85-2,5 m; her boydan 4).
  - **Ölçüldü** (`--clip darbe --set darbe.guc=G`):

    | Güç | Top | Gökdelen, lobi katı kolonu (1,0) | Apartman, köşe kolon (0,0) | Apartman, cephe (x=0, y 4,7, z 6) |
    | --- | --- | --- | --- | --- |
    | 0,75 · Ağır | 1,0 m · 33 t · 39 m/s | | ayağı 99 m savruldu, bina ayakta | içinden geçti, bina ayakta |
    | 1 · Göktaşı | 2,5 m · 520 t · 110 m/s | lobiyi delip geçer, kule çöküp devrilir (çatı 95,8 m iner) | bina göçer (çatı 13,4 m) | bina göçer (çatı 12,2 m) |

    Göktaşının savurduğu parçalar 107-119 m/s'ye çıkıyor (koruma 120). Gökdelende fizik çarpmadan sonra 24-26 ms,
    enkazda (35 bin temas yaması) 31-37 ms.
- **2026-10-02 (gece, son) · Gökdelenin ortası artık donatısız beton.** Özcan: *"gökdelenin ortasında dikdörtgen bir
  alan var. [...] top ata ata yıkarken mesela gökdelenin çevresi yıkılıyor ortasında dikdörtgen kat kat alan kalıyor,
  sonra o da devriliyor ama bütün olarak devriliyor"*, *"fiziği değil, gökdelenin ortasını düzelt sadece"*, *"Merkez çok
  bağımsız kalıyordu"*, *"Biraz bile iyileşse yeterli."* Ekran görüntüsünde çevresi yıkılmış, 90 m'lik, eğilen tek bir
  tüp ayakta.
  - **Sebep:** asansör boşluğunun dört perde duvarı kolon donatısıyla (%1,5) kat kat birbirine ve kirişlere bağlıydı:
    çevresi giden bir konsol tüp.
  - **Yapılan:** çekirdek duvarları donatısız beton (`gokdelen.core_rho = 0`; -1 eski hâli). Basınçta taşır, çekilince
    hiçbir şey tutmaz. Depremde ölçülen aynı: 5 ve 6 kırık 0 (öteleme %0,04 / %0,07), 7 kule devrilir (göçüşte fizik
    15-22 ms, enkazda 26-27 ms), 9 göçer (çatı 96 m). Oturmada çatlak 1.345 (1.265).
  - **Denenip bırakılan:** çekirdek yerine her katta kapılı tuğla asansör boşluğu. Orta da diğerleriyle yıkıldı, ama
    5'te 24, 6'da 619 lif koptu, 7'de kule yerine çöktü ve sıkışık yığında 57 bin temas yaması, adım 53-64 ms: ağır çekim
    geri geldi.
  - **Ölçülen sınır** (`darbe.soy`: orta açıklık dışındaki her şey kaldırılır, sonra ortaya mermi gücünde top): orta
    açıklık iki hâlde de ayakta kalıyor (dört donatılı kolonu ve kirişleri taşıyor). Donatısız çekirdek merkezi zayıflatır,
    sorunu tamamen çözmez; Özcan'ın onayıyla bu kadar.
  - Tanı: `darbe.cevre` (çevre kolonlara kat kat top halkaları: `darbe.katlar`, `darbe.kat_bas`, `darbe.kat_adim`,
    `darbe.aralik`), `darbe.soy`. Panelde gökdelenin notu "camlı lobi". Düzlem döngüsünde ilk dışarıda kalan düzlemde
    çıkış geri kondu (topun 256 düzlemi var; sonuç aynı). Testler 32/34.
- **2026-10-02 (gece) · Gökdelenin göçüşü artık ağır çekimde değil: temas iki kat hızlı.** Özcan: *"Sadece gökdelende
  sanırım fps + büyük yapı olmasından dolayı ağır çekimde gibi yıkılıyor. Bunu optimize edip düzeltir misin. Ağır
  çekimde değilken yıkım yavaş yani."* Ara durumda: *"gerçek zamanlı olması şart değil, iyi bir optimizasyon yapmış
  olduk. %70-80 yeterli."* Sonra: *"belirgin iyileştiyse artık yeterli. İşleri toparlayıp tamamlayabilirsin."*
  - **Sebep:** pencere karede en çok bir fizik adımı atar; adım 16,7 ms'yi geçince dünya yavaşlar. Gökdelenin göçüşünde
    adım 40-50 ms'ydi. Payı temas: enkaz yığınında çift başına ~18 gerçekten iç içe nokta, alt adım başına 4 yineleme,
    karede 40 alt adım.
  - **Ölçüldü** (gökdelen 7; `--set test.perf=1`: her saniye adımın ortalaması ve en kötüsü):

    | | Önce | Şimdi |
    | --- | --- | --- |
    | Ayakta | 11,5 ms | 11-12 ms |
    | Göçüş (t 20-28 sn) | 40-48 ms | 20-23 ms |
    | Enkaz otururken (t 30-35 sn, ~26 bin temas yaması) | 49-50 ms | 25-29 ms |
    | Pencerede (`--autotest`, dünya saati / duvar saati) | ~15-25 fps | göçüşte 40-53 fps; dünya gerçeğin %73'ü (göçüş), %66'sı (göçüş sonu ve enkaz) |

  - **Yapılanlar** (`phys.cu`; ayrıntı "Mimari" ve "Hızlandırmada öğrenilenler"): ayrı dar faz (`kNarrow`, çift başına
    bir iş parçacığı); çift başına 8 şerit (`kTile`, bir warp'ta dört çift); toplam örneği 256'yı aşan büyük çiftler
    tam warp'la yan kolda, ilk yinelemenin noktalarını saklayıp yeniden çözer; örnekler kutulu 32'lik gruplarda;
    temaslar arayüzlerle aynı anda (arayüzler ve zemin temasları yüksek öncelikli kolda, temaslar entegrasyon sonrası
    konumların kopyasında); çiftin konumu float yer değiştirme; `__launch_bounds__(128, 4)`.
  - **Determinizm:** ölçerken gökdelenin 7'deki göçüşü iki koşuda farklı çıktı (24. sn'de 50 bin / 101 bin kırık lif);
    kaydedilmiş kod da öyleydi, apartmanınki değildi. Sebep sürtünme çapası tablosu (yolda başka çiftin bayat yuvasını
    kapmak); tablo artık kareler arasında yeniden kuruluyor. Gökdelen 7 dört koşuda 24. sn'ye kadar birebir, apartman 8
    iki koşuda birebir. **Açık:** gökdelenin 35. sn'si iki koşuda ayrıldı (çatı ötelemesi 75,3 / 76,6 m); ikinci bir
    kaynak var, bulunmadı.
  - Testler 32/34 (aynı iki konsol). Üç yapının tablosu yeniden ölçüldü (aşağıda); nitelik aynı, apartmanın 9'unda çatı
    11,5 yerine 12,7 m iner. Köşe kolona dengeli top: ayak 3,0 m fırladı, gövde devrilip 1,65 m indi.
  - **Açık (önceden de):** ev ayakta 19-21 ms (gerçek zamanın ~%85'i): profilde 28 ms'nin 20,6'sı arayüz renk geçişleri,
    temas değil; evin graf boyaması çok renkli.
  - Tanı: `--set test.perf=1` (saniye saniye fizik), `--set test.profile_at="s s"` (o an bir kare çekirdek çekirdek,
    temas geçişinin parçaları ayrı); `--autotest` artık gökdeleni 7'de göçertip dünya saatini yazar.
- **2026-10-02 (akşam) · Üç yapı: apartman, gökdelen, ev; çözücüde temas payı düzeltildi.** Özcan: *"yıkım moduna 3 çeşit
  bina ekleyelim. Biri zaten bu apartman. Diğeri gökdelen olsun. Çok upuzun bir bina. Gerçekçi upuzun. Zaten beton ve
  duvar temelimiz hazır. Sadece şekli değiştireceğiz. Bir de yer evi olsun. Yine aynı malzemelerle daha basit villamsız
  ev. Üçgen çatılı falan."*
  - Panelin en üstünde **Yapı** (Apartman / Gökdelen / Ev): seçince yapı yeniden kurulur, kamera ona göre
    (`yapi = apartman|gokdelen|ev`; komut satırında `--set yapi=...`). Malzemeler üçünde de aynı.
  - **Gökdelen** (`buildTowerBlock`): 30 kat, 97 m; 3×3 açıklık (5 m); ortada perdeli çekirdek (asansör boşluğu,
    döşemesiz); kolonlar 40×90 cm (30×50'likler yirmi katın altında ezilirdi: ~15 MPa, beton 16); büyük pencereli tuğla
    dolgu; önde zemin katta camlı lobi. 8.404 parça. Oturmada kırık lif 0, tepe 16 mm kısalır. Fizik ~17 ms/kare,
    apartmanla neredeyse aynı (süre parça sayısından değil, renk geçişlerinden gelir; bkz. arayüz rengi notu).
  - **Ev** (`buildHouse`): tek kat ve tavan döşemesi, tuğla duvarlar, kapı, pencereler, iç bölmeler; **beşik çatı**
    (30°, saçakta 0,5 m, beşikte 0,3 m taşma), mahya kirişi ve dikmeleri, baca. 456 parça. Eğik parçalar için
    `Piece::cuts`: kutu + kesme düzlemleri; aynı düzlemle karşı yönden kesilmiş iki parça o düzlemden yapıştırılır
    (çatı şeridi ile saçak takozu, beşik duvarı, mahya kirişi, baca).
  - **Ev oturtulurken ölçülerek bulunanlar** (her biri oturmadaki kırık ve çatlak lif sayısıyla):
    1. 4×4 lif ızgarası kesik bir yüzün ince eğik şeridini kaçırdı; mahyada hiç lif yoktu, beşik tepeleri çatıyı tek
       başına taşıyıp yırtıldı: 163 kırık. Kesik yüzlerde ortak bölge 24×24 örneklenip en çok 16 lif oradan seçiliyor
       (`sharedFibres`): 0 kırık.
    2. Çatının iki yarısı birbirine yaslanan bir A-çerçeve olunca saçakları itti: 2.174 çatlak (çatısız ev: 0;
       ağırlıksız çatı: 0). Mahya kirişi ve iki dikme eklendi.
    3. Mahya kirişi beşik duvarlarının iç yüzüne yapıştırılınca ~30 kN'u onları düzlem dışına 6-9 mm itti. Kiriş artık
       beşiklerin üstüne oturuyor (beşiğin orta sütunu kirişin altında biter). Sonuç: **kırık 0, çatlak 0, en çok kayan
       parça 0,4 mm**.
    4. Beşikte bir sırayla eğim arasında kalan ince kama 32 mm dışarı sıkıştı; şimdi her sütunun üst bloğu son tam
       sıradan eğime kadar.
    5. Kiremitli çatı bir levha değil: şeritler mahya boyunca birbirine yapışık değil (`Piece::unglued`).
  - **Çözücü: Jacobi payı artık çiftin.** Gökdelenin göçüşünde bir kolon parçası 105 m/s ile 190 m'ye çıktı
    (`--set test.track=1`: her saniye en hızlı parça ve temasları). Birbirine saplanmış iki parça arasında saniyelerce
    1,2 MN vardı. Her parça kendi temas sayısına bölünüyordu; biri çok temas ederken çiftin düzeltmeleri birbirini
    götürmüyor, çift kendini itiyordu. Şimdi bir çiftin iki parçası da aynı payı alıyor: 1 / (ikisinin önceki alt adımdaki
    temas sayısından büyüğü), konum ve hız geçişinde (`pairShare`). Gökdelen 9'da en hızlı parça 43,5 m/s (tepeden
    serbest düşüş 43,8); göçüşten sonra her şey 5 sn'de durulur. Ayrıca yapışık olmayan bir cismin 10 cm'den derinine
    saplanmış bir nokta artık itmiyor (`phys.deep_overlap`).
  - Testler 32/34 (aynı iki konsol). Apartmanın bütün tablosu yeniden ölçüldü: nitelik aynı, sayılar biraz değişti
    (aşağıda "Büyüklükler"). Köşe kolona dengeli top: ayak 2,4 m fırladı, gövde devrilip 1,6 m indi (önce 0,18 m kaymıştı).
  - **Gökdelen 7'de yumuşak kat**: zemin kat ~11. sn'de gider, kule eğilir, ~25. sn'de bütün olarak devrilir (en hızlı
    parça 43,7 m/s: devrilen 97 m'lik bir çubuğun ucu ~53 m/s). Denenenler, hepsi 7'de göçtü: lobiyi kapatmak (kısmi göçme,
    çatı 28 m iner), kolonları 50×100 yapmak, ikisi birden. 6'da kırık 0, öteleme %0,07. Model sünek olmayan 1990'lar
    çerçevesi gibi davranıyor; lobili hâli (Türkiye'de yaygın yumuşak kat) bilerek bırakıldı.
  - Performans: gökdelenin göçüşünde fizik 40-80 ms (36 bine kadar temas yaması), pencerede ~15-25 fps.
  - **İngilizce:** sahnenin bütün yazıları iki dilde (uygulamanın dili; `docs/MIMARI.md`).
- **2026-10-02 (öğleden sonra) · Topun gücü çubukla, saniyede en çok bir top.** Özcan: *"Top atma güzel olmuş. Bir top
  atınca 1 saniyeliğine tekrar top atılamasın. spam olmaması için. Bir de deprem şiddeti ayarlamak gibi topun ağırlığını
  yani gücünü ayarlayabilelim. Kaydırma çubuyla. Mesela şuanki hali dengeli, hafif zarar veriyor güzel. Daha hafif böyle
  çarpıp pek etki etmeyen seviyeden en güçlüde binanın içinden geçip giden hızlı ve ağır yapabiliriz. Mermi gibi yani.
  Fizik o topun etkisine göre doğal hesaplanır şuanki gibi."*
  - **Bekleme:** bir toptan sonra 1 sn (duvar saatiyle; ağır çekim uzatmaz) tıklama yok sayılır. `--autotest`: üç
    tıklama, ikincisi 0,3 sn sonra: iki top atıldı (günlükte).
  - **Güç çubuğu** (panelde en üstte "Top"; ↑ ↓ ile de): boy ve hız birlikte (`YikimApp::ballThrow`). Ortası (çentik)
    bugünkü dengeli atış (0,5 m, 4,1 t, 14 m/s); sola doğru küçük ve yavaş (en hafifi 0,25 m, 0,52 t, 8 m/s), sağa doğru
    büyük ve çok hızlı (en güçlüsü 0,7 m, 11,4 t, 70 m/s = saatte 252 km), her yarı log ölçekte. Toplar yedi boyda
    hazır bekler (0,25-0,7 m, her boydan 4); hız sürekli, boy en yakın hazır top. Ctrl ile ağır top kalktı (çubuk onun işini görüyor).
    Hızlı top daha geriden belirir (çeyrek saniyelik uçuş), görülsün diye.
  - **Ölçüldü** (`--clip darbe --set darbe.guc=G`; bina her koşuda ayakta, çatı ≤ 1 mm):

    | Güç | Top | Köşe kolon (0,0) | Cephe, 1. kat (x=0, y 4,7, z 6), dik |
    | --- | --- | --- | --- |
    | 0 · Hafif | 0,25 m · 0,52 t · 8 m/s | ≤ 1 cm kıpırdadı, top sekti | |
    | 0,25 · Orta | 0,35 m · 1,4 t · 11 m/s | 2-3 cm, kırılmadı | |
    | 0,5 · Dengeli | 0,5 m · 4,1 t · 14 m/s | ayağı 2,1 m fırladı, gövde eğik asılı | tuğla katın içinden geçti (363 kırık lif) |
    | 0,75 · Ağır | 0,6 m · 7,2 t · 31 m/s | orta parça 42 m savruldu (28 m/s) | geçti (443 kırık lif), en hızlı parça 23 m/s |
    | 1 · Mermi | 0,7 m · 11,4 t · 70 m/s | kolon parçası 98 m savruldu (63 m/s) | geçti, 1.261 kırık lif, tuğla 77 m/s; top 250 m öteye |

    Fırlayan parçanın hızı fiziğin söylediği kadar: ağır topun çarptığı hafif cisim topun hızının en çok (1 + sekme)
    katını alır (70 × 1,15 ≈ 80 m/s). Fizik süresi bu atışlarda da ~15 ms/kare; en hızlı parça 120 m/s korumasının altında.
    Dolgu tuğlası dengeli topu bile durdurmuyor: 4 tonluk çelik top tuğla bölmeden geçer (gerçekte de).
- **2026-10-02 (gündüz) · Yıkım topu, deprem süresi, tıklanan panel.** Özcan: *"Yıkımda depremlerin süresi varsayılan
  şuanki gibi olsun ama biz değiştirebilelim. Sonsuza kadar da sürdürebilelim. Durduruna kadar yani. Deprem
  büyüklüklerine mouse ile tıklanabilsin. Süresi kaydırma çubuğu şeklinde ayarlanabilsin. Bir de mouse ile binaya
  tıklayınca kolon pıt diye yok oluyor ya. Gerçek bir darbe gibi hissettirsin. Ondan pıt diye yok olmasın. Biri vurmuş
  gibi kırılıp devrilsin. Daha mantıklı olur."*
  - **Yıkım topu** (`YikimApp::strike`): tıklanan noktaya kameranın yanından döküm çelik bir top gelir; noktanın 6 m
    önünde belirir, 14 m/s ile varır (yerçekiminin yolda alacağı kadar yukarı nişanlı: tam noktaya düşer). Top öteki
    parçalar gibi bir cisim (`polySphere`: 256 teğet düzlemle kırpılmış küp; görüntüde kürenin normaliyle düz gölgelenir):
    neyin kırılıp devrileceğine çözücü karar verir. Kurulurken toplar zeminin 1000 m altında donuk bekler
    (`parkBody`), atılınca kendi kütleleriyle döner (`launchBody`); 6 normal + 3 ağır, en eskisi yeniden atılır.
    Patlatma (cismi kaldırma) canlı sahneden kalktı; `kolon` senaryosu komut satırında duruyor.
  - **Ölçüldü** (`--clip darbe`, köşe kolon (0,0), zemin kat kolonunun ortasının 0,5 m altı):

    | Top | Kolon | Sonuç |
    | --- | --- | --- |
    | 0,35 m · 1,4 t | köşe | kolon 7-9 cm kaydı, kırılmadı, top sekip cephe boyunca yuvarlandı |
    | **0,5 m · 4,1 t** (sol tık) | köşe | ayak parçası 2,1 m fırladı (8,2 m/s), orta parça eğilip asılı kaldı, donatı filizleri; bina ayakta |
    | 0,5 m · 4,1 t | kenar (1,0) | ayak 0,83 m, orta parça 0,38 m: kırıldı, eğik |
    | **0,7 m · 11,4 t** (Ctrl) | iç (1,1) | iki alt parça ~7 m savruldu (en hızlı parça 12,9 m/s; top 14 m/s), top binanın altından geçti; bina ayakta (çatı 1 mm) |

    1,4 t'luk top neden yetmedi: katı parçalı modelde darbe yerel ezme yapamaz, bütün 2,2 m'lik parçaya dağılır; iki
    arayüzün kayma kapasitesi (~930 kN) topun momentumunu ~0,02 sn'de yer, kayma 8 cm kopma eşiğine varmadan biter.
    Gerçekte çelik topun kısa temas süresi yerel ezme yapardı; burada yetecek enerji kütleyle verildi.
  - **Deprem süresi** (`QuakePlay`, `quakePlay`): kendi süresinde ve daha kısada kayıt o uzunlukta kesilir (kendi
    süresinde ölçülen kaydın aynısı: büyüklük 7 yeniden koşuldu, kırık lif 3.024, öteleme %0,66, değişmedi). Daha uzun
    ya da **durdurana kadar**: kaydın güçlü kısmı **dikişsiz döngü** olur (sonu başına eşit güçle karışır; döngü
    boyunca ivmenin ve hızın ortalaması çıkarılarak iki kez integre edilir, böylece hız ve yer değiştirme kendi
    üstüne kapanır, zemin kaymaz), kaydın kendi yükseliş ve sönüşüyle bir zarfın altında oynar. **Durdurmak** zemini
    1,5 sn'de dindirir (eskiden bir anda donuyordu: hızın tek adımda sıfırlanması bir sarsıntıdır).
    Test `--test dongu` (4/4): dikişte yer değiştirme, hız ve ivme adımı döngünün içindeki en büyük adımın en çok
    0,50 / 0,45 / 0,33'ü; zeminin en uzak gittiği kendi kaydınınkinin 0,75-1,08 katı (büyüklük 9: 1,51 m, kaydı 1,45 m).
  - **Panel:** büyüklükler tıklanır; **süre çubuğu** 2-180 sn (log), çentik büyüklüğün kendi süresi (yakınına
    bırakınca ona yapışır), en sağ ∞; **← →** yuvarlak sürelerden geçer; **Depremi başlat / durdur** düğmesi (Boşluk).
    Süre bir sonraki depremde geçerli (sürerken değişirse panel söyler). Sanal fareyle `--autotest`: büyüklük 7'ye
    tıkla, çubuğu ∞'a çek, başlat (49 fps), durdur (düğme "Sarsıntı diniyor"), çubuğu çentiğe bırak (26 sn).
  - Testler 28/30 (bilinen iki: konsol) + döngü 4/4. Bekleyen toplar fiziği değiştirmiyor: büyüklük 7 aynı sonucu verdi
    (kırık lif 3.024, öteleme %0,66); fizik süresi topsuz 14,2-15,3 ms, toplu 14,8-15,3 ms (sıra değiştirilerek, gürültü içinde).
- **2026-10-02 · Kamera yönleri düzeltildi, depremler büyüklükle (3-9), sahne tek uygulamada.** Özcan (gece, otonom
  çalışma isteği): *"Yıkım simülasyonunda fare ve klavye hareketleri ters, onları düz yap yani sağa-sağa, sol-sola
  gitsin."* ve *"Yıkım için Türkiyedeki depremleri kaldır, onun yerine onlardan ilham alarak 3 şiddetinde 4 5 6 7 8 9
  şeklinde rihter ölçeğine göre depremleri listele. Yani şu deprem yerine 6 şiddetinde deprem diyelim."*
  - **Kamera:** görüntünün sağı `ileri × yukarı` (renderer böyle kuruyor); uçuş kodu D için `(cos, 0, -sin)`, yani
    ekranın solunu kullanıyordu, fare de sapmayı ters yönde artırıyordu. Kamera tek yerde (`fly_camera.h`);
    `--scene yikim --test kontrol` öndeki bir noktanın D'de ve fare sağa giderken ekranda sola kaydığını sınar (4/4).
    Uygulama içinde sanal fareyle de doğrulandı: sağ tuşla sağa sürükleyince bina ekranda sola kayıyor.
  - **Büyüklükler** (`quake.cpp`, `kMagnitudes`): her biri bir gerçek kaydın dalgası; güçlü sarsıntı süresi kadar
    kesilir (tepeden önce üçte bir), küçükler hızlı oynatılır, o büyüklüğün tepe ivmesine ölçeklenir, kosinüs
    yükseliş ve sönüşle; kesilen kayıt sonda hareket ettiği için yumuşak bir taban çizgisi düzeltmesi (başta eğimsiz,
    sonda değeri ve eğimi sıfırlayan kübik; ivmesi ~6d/T², milimetre/s²) zemini başladığı yerde durdurur.
    Pencerede **3-9** büyüklük, **Boşluk** başlat; **+/-** ve kayıt tuşları kalktı. Ölçümler aşağıda ("Büyüklükler").
  - Sahne artık `build\pc_graphics.exe`'de, galeriden açılıyor (ortak arayüz: büyüklük seçici, sarsıntı ilerlemesi);
    `yikim.exe` kalktı, araçlar `--scene yikim --test|--clip|--still`. Kod `src/scenes/yikim/`. Uygulamanın panosu
    `docs/DURUM.md`.
- **2026-10-02 · Gren giderildi.** Özcan: *"Görüntü noktacıklı yani çok grenli gibi. Estetik bir durum [...] Bir hata
  olduğunu sanmıyorum. Eğer öyle olması gerekiyorsa düzeltmek yerine nedenini de açıklayabilirsin."* Sebep: ışın
  izleme, gök ışığı (tek kosinüs ışını) ve güneş diski (tek gölge ışını) karede 6 örnekle; dünya durunca örnekler
  birikip temizleniyor, bir şey hareket edince her kare sıfırdan. Çözüm: **kendi gürültü gidericimiz**
  (`src/yikim/yikim_filter.cu`, hafif SVGF), 2,4 ms, pencere ~54 fps; **G** tuşu süzgeç → OptiX yapay zekâ → ham.
  Özcan otomatik testi izlerken: *"o noktacıklanma, grenlenme düzelmiş görünüyor"* (1. süzgeç), 2. ham grenli,
  *"3.yü açınca yine temiz görüntü"* (yapay zekâ).
- **2026-10-02 · Fırlayan tuğlalar düzeltildi.** Özcan (kapanıştan sonra): *"Fizik kısmı tamam, çok iyi olmuş. [...]
  Yıkılma fiziği hem çok gerçekçi hem de tutarlı hissettiriyor. Sadece arada hafif parçalar uçuşabiliyor, kolonlar
  değil de kahverengimsi duvar gibi olanlar. Onlar her zaman değil bazen sallanırken düşmek yerine fırlıyor. Fizik
  anlamında sadece onu düzeltebilirsin."* Sebep: iç içe geçmeyi itme hızı sınırı (2 m/s) hafif bloğu fırlatıyordu
  (ayrıntı "Kapanışta öğrenilenler"). `phys.max_depenetration` 2 → **0,5 m/s**. Binadan fırlayan tuğla Pazarcık ×1'de
  93 → 1, Antakya çöküş öncesi 44 → 0, Yarımca ×1 13 → 0, Bayraklı 3 → 0. Testler 24/26, her kaydın sonucu aynı.
  Yan yana klip: `captures/yikim/y3_firlama_once_sonra_pazarcik.mp4`. Tanı aracı: `--set test.kick=2 test.kick_min_y=1`.
- **2026-10-02 · 🏁 Sahne 3 kapandı.** Özcan (Y2 klibi üzerine): *"Tamam. Video yine iyi duruyor. Artık son eklemeleri,
  düzenlemeleri yapıp tamamlayabilirsin."* Kapanışta yapılanlar:
  - **Canlı pencere hızlandı: 28-30 → 55-75 fps** (1920×1080, 6 örnek; koşudan koşuya, fizik saniyede en çok 60 adım). Fizik bir sonraki adımı hesaplarken görüntü
    öncekini çiziyor (iki akış, aralarında bir olay); karede en çok bir fizik adımı.
  - **Donatı görünümü** (Y3'ün parçası): lif tuttukça çatlağın üstünden geçen, koptuğunda yüzden eğri çıkan demirler;
    **B** gizler. Maliyeti ölçülemeyecek kadar küçük.
  - **Fizik hatası düzeltildi:** balkon döşemesinin donatı oranı ona değen tuğlaya (korkuluk) sızıyordu: dayanımı sıfır,
    ama tabanı 1,8 kat katılaştıran "hayalet demir". Donatı görünümü buldu (yerdeki korkuluk parçalarından demir
    çıkıyordu). Deprem tablosu yeniden ölçüldü (aşağıda). Sonuç: Pazarcık ×1 artık **ağır hasarla ayakta**
    (önceden göçüyordu), ×1,25'te göçüyor; öbür kayıtlar aynı sonucu verdi. "Çatı ötelemesi" ölçüsü de düzeltildi
    (korkuluğu ölçüyordu).
  - README (EN + TR) üç sahneyle güncellendi (sahne 2 hiç eklenmemişti), `GOSTERI.md`'ye `yikim.exe` tuşları.
  - Açmak: `build\yikim.exe` (çift tık, tam ekran). Testler 24/26 (kalan iki: konsolun elastik yumuşaklığı).
  - **Yapılmadı (bilerek):** Y4 toz, Y5 görüntü (Özcan'ın ışık kararı "sade güneşli gün"), Y3'ün geri kalanı (pürüzlü
    kırık yüzey, sıva, cam). İstenirse açılır.
- **2026-10-01 · Y2 (deprem) benim tarafımda bitti; Özcan'ın gözü bekleniyor.** Özcan (Y1 kapanışı): *"Çok iyi duruyor.
  Devam edebilirsin."* Y2'de: **determinizm** (aynı deprem aynı binayı her koşuda aynı yıkar), **sönüm %5,3** (kütleye
  orantılı, zemine göre), bağlı çiftlerde derin iç içe geçmeye temas, ayrılma hızı cırcırı kapandı (en hızlı parça
  ≤ 18 m/s). Klip `captures/yikim/y2_ayni_bina_iki_siddet.mp4`: Yarımca ×1,0 (ayakta) ve ×1,5 (yumuşak kat göçmesi) yan yana.
- **2026-10-01 · Y1 (apartman) benim tarafımda bitti; Özcan'ın gözü bekleniyor.** 5 kat (dükkânlı zemin 3,5 m + 4 × 2,9 m),
  3 × 4 açıklık (12 × 16 m), kolon 30×50, tuğla dolgu duvarlar (pencere ve balkon kapısı boşluklu), ön balkonlar, düz
  çatı + korkuluk + binaya bağlı olmayan 3 su deposu ve 3 güneş kollektörü: 3007 cisim, 6857 arayüz, 44 378 lif.
  Klipler: `captures/yikim/y1_kolon_kaldirma.mp4` (ön iki hattın 8 zemin kat kolonu kaldırılır: ön yarı göçer, arka
  yarı ayakta), `y1_deprem_izmir_bayrakli.mp4` (2020 Bayraklı: bina ayakta, korkuluklar ve duvar blokları dökülür),
  `y1_deprem_kocaeli_yarimca.mp4` (1999 Yarımca: yumuşak kat göçmesi). `scene = bina` varsayılan, `scene = cerceve` Y0.
- **2026-10-01 · Y0 kapandı, Y1 (apartman) başladı.** Özcan, iki klip üzerine: *"fizik yeterince iyi duruyor ya hız ve
  hissiyat olarak. Devam edebilirsin."* Çatı kararı: **düz çatı + su depoları ve güneş kollektörleri** (binaya bağlı
  değil, depremde kayar ve düşer).
- **2026-10-01 · Y0: fizik çekirdeği + sade görüntü + iki klip; Özcan'ın gözü bekleniyor** ("ağırlık hissi").
  Testler 19/21 (kalan iki: konsol kirişin elastik yumuşaklığı). Klipler `captures/yikim/y0_kolon_kirma.mp4` (ön hattaki
  üç kolon sökülür, ön açıklıklar göçer, döşemeler arka hattan asılı kalır) ve `y0_deprem_pazarcik_x1.5.mp4` (Pazarcık
  kaydı ×1,5: zemin kat kolonları mafsallaşır, yumuşak kat göçmesi, bina enkaza döner).
- Çalıştırma: `build\yikim.exe --test [ad]`, `--clip kolon|sarsinti --seconds S --out captures/yikim/ad` (30 fps kareler +
  mp4), `--still T --scenario ad --out ...`; ayarlar `scenes/yikim.cfg`, `--set anahtar=deger`; tanı `--set test.verbose=1`,
  `test.profile=1`, `test.elastic=1`.

## Fazlar
| Faz | Durum | Not |
|---|---|---|
| Y0 Fizik çekirdeği | ✅ 10-01 | çözücü, testler, sade görüntü, iki klip; Özcan: hız ve his yeterli |
| Y1 Bina | ✅ 10-01 | apartman, testler, üç klip; kesit ısı haritası yapılmadı (tanı araçları yetti); Özcan: "Çok iyi duruyor" |
| Y2 Deprem | ✅ 10-01 | kayıtlar, otomatik başlangıç, determinizm, sönüm, iki şiddet klibi; Özcan: "Video yine iyi duruyor" |
| Y3 Kırılma görünümü | ◐ 10-02 | yalnız donatı (çatlakta köprü, kopunca filiz); pürüzlü yüzey, sıva, cam yapılmadı |
| Y4 Toz | ✖ | yapılmadı: sahne kapandı, odak fizikti |
| Y5 Görüntü | ✖ | yapılmadı: Özcan'ın ışık kararı sade güneşli gün |
| Y6 Canlı | ✅ 10-02 | kamera, patlatma (sol tık, Ctrl büyük), deprem tuşları, ağır çekim, HUD, 55-60 fps |
| Y7 Fizik turları | ✅ | her fazın klibi Özcan'ın gözünden geçti (Y0, Y1, Y2 onayları) |

## Mimari (Y0)
- `src/yikim/`: `geom` (dışbükey çokyüzlü, kesme, kütle ve eylemsizlik), `phys.h/.cu` (ekran kartı çözücüsü),
  `yikim_scenes` (test düzenekleri, kutulardan otomatik bağ, betonarme çerçeve), `yikim_tests` (`--test`),
  `yikim_app` (giriş). `yikim.exe` = `pc_graphics.exe --scene yikim`.
- **Katı parçalar + lifli arayüzler (AEM'den esinli).** Değen iki kutu bir arayüzle yapışır; arayüz yüzeye yayılmış
  2-4 × 2-4 lif taşır (beton alanı + çevre liflerine dağıtılmış donatı). Lif konumları kesitin gerçek atalet
  momentini verecek kadar dışa itilir (n nokta için n/√(n²-1)). Malzeme yasası liflerde: beton çekmede çatlar
  (donatı yoksa açılan çatlak boşluktur, kalıcı değil), basınçta ezilir (kalan kapasite %30); donatı akar, 6 cm plastik
  uzamada kopar; kayma önce kohezyon, çatlayınca sürtünme + dübel. Sınırı aşan kısım kalıcı (plastik) ofsete gider.
- **Çözücü: XPBD, küçük adımlar.** Varsayılan 40 alt adım × 2 arayüz geçişi, 60 Hz kare. Arayüzler graf boyamayla
  renklenir, renk renk Gauss-Seidel; arayüz içinde lifler sırayla, dönmeler birinci dereceden (arayüz başına bir kez
  matris). Konumlar double (20 m'de float 2 µm çözer, alt adım hareketi µm).
- **Temas: önce dar faz, sonra çift başına 8 şerit** (10-02 gecesine kadar çift başına bir warp; bkz. "Hızlandırmada
  öğrenilenler"). Dar faz çift başına bir iş parçacığıyla kutu testini yapar, değebilecek çiftleri listeler; bir warp
  dört çifti çözer, iki cismin toplam örneği 256'yı aşan büyük çiftler tam warp'la yan kolda. Bir cismin örnek noktaları
  (yüze 5 mm içeri çekilmiş köşe, kenar ve yüz noktaları; kutusu olan 32'lik yakın gruplarda, karşı kutuya erişmeyen
  grup atlanır) ötekinin düzlemlerine karşı. Her yinelemede noktaların normal düzeltmesinin ortalaması, sonra yamanın
  merkezinde tek sürtünme; 4 yineleme (büyük çift sonraki yinelemelerde ilkinin bulduğu noktaları yeniden çözer). **Sürtünme çapası** (PhysX gibi): ilk temasta yama merkezi iki cisme iğnelenir, statik sürtünme
  iğneleri μs·N bütçesiyle birleştirir; tutamazsa kayar (bütçe μd·N, iğneler taşınır). Hız geçişi yalnız kalan
  bütçeyi harcar; çarpma katsayısı ve burulma sürtünmesi yama merkezinde. Cisimler arası Jacobi (çift sayısına bölünür).
- **Alt adım döngüsü bir CUDA Graph**; ayar ya da dt değişince yeniden yakalanır. Zemin = 0. cisim (kinematik),
  her alt adımın zemin konumu cihaza yazılır (deprem için hazır). Alt adımda arayüzler (ardından zemin temasları)
  yüksek öncelikli bir kolda, cisim temasları onlarla aynı anda: temaslar entegrasyondan hemen sonraki konumların
  kopyasını okur, düzeltmeleri `kApply`'da arayüzlerin düzelttiği konumlara eklenir (iki grup arasında Jacobi).

## Ölçümler (RTX 4080, `--test`, varsayılan 40×2)
| Test | Ölçülen | Beklenen |
|---|---|---|
| serbest düşme 3 m, varış | 0,7819 s | 0,7821 s |
| 5 sn sonra gömülme | 0,50 mm (= tolerans payı) | -3..+2 mm |
| kule 10 + piramit 15, 30 sn | kayma 0,6 mm, çökme 5,3 mm | < 10 mm |
| eğik düzlem 25° (μs 0,7) | 1,4 mm | < 10 mm |
| eğik düzlem 40°, 1 sn | 0,951 m | 0,898 m ± %15 |
| enerji (40 kutu yığını) | hiçbir an artmıyor | |
| çerçeve 2 kat 2×2 açıklık, 279 cisim, öz ağırlık | sehim 1,0 mm, kopma 0, 10. sn hız 2,3 mm/s; 244 lif çatlak | |
| **konsol kiriş 5 m, 10 parça** | **sehim 2,9×, frekans 0,59× analitik: KALDI** | 0,7..1,5 |
| mafsal: 6 m kiriş konsolu (66 kN·m, kesit ~38) | 3 sn'de 4,7 m düşer, donatı kopar | düşer |
| mafsal: 3 m kiriş konsolu (16,5 kN·m) | 2 mm | durur |
| mafsal: 2 m döşeme konsolu (10,6 kN·m, kesit ~3,7) | 1,7 m düşer | düşer |
| mafsal: 0,6 m döşeme konsolu | 0 mm | durur |
| fizik süresi, çerçeve | 5,0 ms/kare (40×2); 20×1'de 1,5 ms; çöküş sırasında ~7 ms | |
| görüntü (1280×720, 12 örnek) | ~1 ms/kare | |

**Senaryo ölçümleri (2 kat, 2×2 açıklık, duvarsız):** tek köşe kolon sökülünce bina ayakta (köşe kirişlerle taşınır);
ön hattaki iki kolon sökülünce ön kirişler 2,2 cm sarkıp durur (arka açıklıklardan sürekli döşeme ve yan çerçeve
taşıyor); üç kolon (bütün ön hat) sökülünce ön açıklıklar 1,4-2,5 sn içinde göçer. Kontrol: tek açıklık, iki kolon
sökülünce 1 katta da 2 katta da tam göçer (talep ~243 kN·m, kapasite ~90). Deprem: Pazarcık ×1,0 → çatı ötelemesi %2,5,
kalıcı eğilme, kopma yok; ×1,5 → yumuşak kat göçmesi; Antakya ×1,0 → %0,12, hasarsız.

**Apartman (Y1):** oturduktan sonra 10 sn kopma 0, sünme 0,1 mm; birinci periyot (x) **0,452 s** (yönetmelik formülü
0,07·H^0,75 = 0,54 s; dolgu katılaştırır). Fizik 12-20 ms/kare (40×2), canlı için Y6'da hızlanmalı. Gerçek kayıtlar
(en güçlü anından 15 sn önce başlar, `deprem.offset = -1`):

⚠️ *Bu tablo Y1 hâlidir (sönümsüz, deterministik değil, bağlı çiftler iç içe geçebiliyordu); güncel olan Y2 tablosu aşağıda.*

| Kayıt (×1) | Yatay tepe | Sonuç |
|---|---|---|
| 2020 Bayraklı | 0,12 g | ayakta, %0,6 öteleme, korkuluklar ve bazı duvarlar dökülür |
| 1999 İzmit | 0,25 g | ayakta (%0,3); düzeltmeden önceki bir koşuda kısmi göçme |
| 1999 Yarımca | 0,34 g | **sınırda:** 3 koşunun 2'sinde tam göçme (atomik toplamlar yüzünden deterministik değil) |
| 2023 Antakya | 0,67 g | tam göçme |
| 2023 Pazarcık | 2,25 g (bileşen) | tam göçme |

Kolon kaldırma (zemin kat): 2 köşe kolonu → ayakta; ön hattın 4'ü → ayakta (üst katlar dolgu + çerçeve kafes kutu,
döşeme donatısı çekme başlığı); ön iki hattın 8'i → ön yarı göçer.

**Y2 (güncel, deterministik, sönüm %5,3):**

| Kayıt | Sonuç (çatının zemine göre en büyük ötelemesi) |
|---|---|
| 2020 Bayraklı ×1 (0,12 g) | ayakta, %0,24 |
| 1999 İzmit ×1 (0,25 g) | ayakta, %0,28 |
| 1999 Yarımca ×1 (0,34 g) | ayakta, %0,69; duvarlar ve korkuluklar dökülür |
| 1999 Yarımca ×1,5 | tam göçme (yumuşak kat) |
| 2023 Antakya ×1 (0,67 g) | tam göçme |
| 2023 Pazarcık ×1 | tam göçme |

**Kapanış (10-02, korkuluk düzeltmesinden ve çatı ölçüsü düzeltmesinden sonra; `--still 45`):**
⚠️ Yukarıdaki Y1 ve Y2 tablolarında "çatı" en üstteki parçaların ortalamasıydı, yani **tuğla korkuluk**; ayakta kalan
binalarda korkuluk yerinde kaldığı için öteleme sayıları yaklaşık doğru, ama korkuluk savrulunca anlamsız. Artık çatı =
taşıyıcı sistemin en üst parçaları (döşeme, kiriş, kolon başı).

| Kayıt | Çatının zemine göre en büyük ötelemesi | Sonuç |
|---|---|---|
| 2020 Bayraklı ×1 | %0,29 | ayakta |
| 1999 İzmit ×1 | %0,28 | ayakta |
| 1999 Yarımca ×1 | %0,91 | ayakta |
| 1999 Yarımca ×1,5 | | tam göçme, çatı 13,0 m iner |
| 2023 Antakya ×1 | | tam göçme, çatı 12,8 m iner |
| 2023 Pazarcık ×1 | %1,34 (43,6. sn; 90 sn'ye kadar artmıyor) | **ağır hasarla ayakta** (korkuluklar, dolgu duvarlar savrulur); düzeltmeden önce tam göçüyordu |
| 2023 Pazarcık ×1,25 | | tam göçme, çatı 12,4 m iner |
| 2023 Pazarcık ×1,5 | | tam göçme, çatı 12,9 m iner |

**Güncel (10-02, iç içe geçme itme sınırı 0,5 m/s; aynı ölçüm; son sütun: binadan fırlayan tuğla, yerden 1 m'nin
üstünde bir karede 2 m/s'den fazla hızlanan, eski sınır → yeni):**

| Kayıt | En büyük öteleme | Sonuç | Fırlayan tuğla |
|---|---|---|---|
| 2020 Bayraklı ×1 | %0,29 | ayakta | 3 → 0 |
| 1999 İzmit ×1 | %0,26 | ayakta | |
| 1999 Yarımca ×1 | %0,70 | ayakta | 13 → 0 |
| 1999 Yarımca ×1,5 | | tam göçme, çatı 12,5 m iner | |
| 2023 Antakya ×1 | | tam göçme, çatı 12,3 m iner | çöküşten önce (16,6 sn) 44 → 0 |
| 2023 Pazarcık ×1 | %0,81 | ağır hasarla ayakta | 93 → 1 |
| 2023 Pazarcık ×1,25 | | tam göçme, çatı 12,1 m iner | |
| 2023 Pazarcık ×1,5 | | tam göçme, çatı 12,5 m iner | |

Göçmelerde yerdeki enkazda hâlâ "fırlama" sayılıyor (Pazarcık ×1'de yerden 1 m'nin altında 15 parça): düşen blok yerde
yatan bloğa çarpınca alttaki bir kenarı üstünde dönerek kalkıyor (tırmık gibi). Fiziksel olarak mümkün; dokunulmadı.

Testler 24/26 (periyot 0,448 s, sönüm 0,053: değişmedi).

Bina testi: sönüm (serbest titreşim, tepelerin logaritmik azalması, en küçük kareler) **0,053**; periyot 0,449 s
(sönüm değiştirmedi). Determinizm testi: aynı yığın iki koşu, fark **0 m**; bina ölçeğinde Yarımca iki koşu birebir.
Testler 24/26.

**Bilinen sınır:** uzun rijit zincirler (konsol) varsayılan ayarda yumuşak; 60×2'de 1,9× (7,5 ms). Binanın gerçek
yumuşaklığı Y1'de doğal periyotla ölçülecek; gerekirse alt adım artar.

## Y0'da öğrenilenler (ölçülerek)
- **Ölçüm aracı sustu:** `--set phys.substeps` teste hiç ulaşmıyordu (yapılandırma işaretçisi atanmamıştı); üç farklı
  ayar birebir aynı sonucu verince fark edildi. Aynı sayı = ayar ulaşmıyor olabilir.
- **Blok sayısı hatası:** bağ çekirdeği 64 iş parçacıklı bloklarla başlatılıp blok sayısı 128'e göre hesaplanıyordu;
  her rengin ikinci yarısı hiç çözülmedi. Küçük sahnede renk 64'e sığdığı için görünmedi; sonsuz dayanımlı elastik
  çerçeve bile 2,9 m çökünce ve yalnız 2 kat × 2×2 açıklıkta çöküp 1×1'de çökmeyince bulundu.
- **Sıcak başlatma kararsız:** önceki alt adımın kuvvetini önce uygulamak, yakınsamamış Gauss-Seidel'in artığını
  integre ediyor; payın 0,5-1 her değerinde konsol patladı. Kaldırıldı.
- **Nokta başına sürtünme çok noktada çalışmıyor:** ilk nokta bütün cismin kaymasını yükün 1/20'siyle durdurmaya
  çalışıyor, koşul hiçbir noktada tutmuyor; 25°'de 5 mm/s sürünme. Yama merkezinde tek sürtünme gerekiyor.
- **Tek geçiş yama sürtünmesi yetmiyor:** ağırlık merkezinin altındaki düzeltmenin ~%60'ı dönmeye gidiyor, normal
  temaslar onu geri alıyor; normal ve sürtünme birlikte yinelenmeli.
- **Sıralı (Gauss-Seidel) normal düzeltme düz düşen kutuyu döndürdü** (1,7 rad/s, yana 0,86 m/s): ilk köşe bütün
  düzeltmeyi yapıyor. Çift içinde ortalama alındı.
- **Yalnız alt adım içi kaymayı düzelten sürtünme kalıcı itmeleri unutuyor:** piramit 30 sn'de 0,65 m açıldı. Çapa ile 0,6 mm.
- **Sürtünme çift sayıldı:** konum geçişinde harcanan statik sürtünmenin üstüne hız geçişi dinamik sürtünmeyi tekrar
  uyguladı; 40°'de blok tutundu. Hipotezim "normal yük şişiyor" idi; ölçünce yük tam m·g·cos θ çıktı (1,00), hipotez yanlıştı.
- **Maliyet çekirdek başlatma değil, seri iş:** CUDA Graph kazandırmadı. Olay zamanlayıcıyla profil: temas 3,4 ms (iş
  parçacığı başına 640 seri örnek testi, bellek beklemesi) → çift başına warp ile 0,3 ms; arayüz 2,6 → 1,45 ms
  (döngüden double ve kuaterniyon çıkarıldı). Kare 4,9 → 1,5 ms (20×1).
- **Simetrik Gauss-Seidel (tek geçişler geri) konsolu daha yumuşak yaptı** (13,3 mm vs 9,4); geri alındı.
- **Kapasite her geçişte ayrı sınırlanınca iki geçiş iki kat taşıdı:** 6 m konsol (66 kN·m) 38 kN·m'lik kesitle
  8,7 cm sarkıp durdu, iki kolonu sökülen çerçeve sarkmadı. Çarpan alt adım içinde biriktirildi (standart XPBD:
  dl = -(C + α̃λ)/(w + α̃)), sınır toplama uygulandı; konsol 4,7 m düştü. Kesitleri tek başına sınayan "mafsal"
  testleri bu yüzden var: çerçevenin davranışı ancak parçaları doğruysa yorumlanabilir.

## Y1'de öğrenilenler (ölçülerek)
- **Yerçekimi bir anda açılınca uzun bina ilk karelerde jöle gibi sarkar:** balkon serbest düşmenin yarısıyla iniyor,
  ince korkuluk tabanı kırılıyordu (528 kopuk lif). Yerçekimi 0,5 sn'de yumuşakça açılıp bina 2 sn görünmeden oturunca
  kopma 0, çatlak 12. Tanı sırası: sonsuz dayanım (yapıştırma doğru) → balkonsuz (kopma yok) → ilk kopan bağlar
  (balkon-korkuluk) → o bağın kare kare izi (balkon ilk karede 0,085 m/s iniyor).
- **Bağları kesilen ama üstü yüklü kolon kolon olmaya devam eder:** dört katın altında temas ve sürtünmeyle taşıyor;
  8 kolon kesilip itildi, hiçbiri kımıldamadı. "Kolon kaldırma" = cismin sahneden çıkması (`removeBody`).
- **İç içe geçmeyi düzeltmek alt adımlar boyunca hız biriktirir:** düşen enkazın zemine gömdüğü blok 22-52 m/s ile
  fırladı (yakalayıcı: `--set test.catch=22`). Hız geçişinde ayrılma hızı, sekme hızı ya da önceki ayrılma hızı ile
  2 m/s'nin büyüğüne kırpıldı; tek karede fırlama kalmadı, en hızlı parça 16-23 m/s (devrilen yapının sürüklediği).
- **Ayar dosyasında "x, y, z" ilk virgülde okumayı durduruyordu** (`sscanf("%f %f %f")`): kamera, itme yönü sessizce
  varsayılandaydı. Virgül de kabul ediliyor.
- **Kayıtların en güçlü anı farklı saniyelerde** (Pazarcık 39, İzmit 52,5, Yarımca 59,9 (yatay vektör), Antakya 72,4):
  sabit 25 sn başlangıç Antakya'yı ve Yarımca'yı sessiz kısmında oynattı ("hasarsız" sonucu yanlıştı). Otomatik başlangıç.
- **Akış yarışı:** sıfırlayan `cudaMemset` varsayılan akışta, fizik engellemeyen akışta; ikinci dünyanın ilk karesinde
  yerçekimi 0 kaldı (kutu bir kare geç düştü). Yüklemenin sonunda aygıt eşzamanlanıyor.
- **Eşikteki yapı kaotiktir:** aynı kayıt aynı binayı bir koşuda yıkıp bir koşuda yıkmayabilir; tek koşudan hüküm verme.

## Y2'de öğrenilenler (ölçülerek)
- **Kayan noktalı atomik toplama deterministik değil:** toplama sırası koşudan koşuya değişiyor, eşikteki bina bir koşuda
  yıkılıp öbüründe yıkılmıyordu. Düzeltme sabit nokta (2^-40) tamsayı toplama; geniş faz çift sırası ve çapa tablosu
  sırası sonucu değiştirmiyor (her çiftin hesabı sırasından bağımsız).
- **Sönümün üç denemesi:** (1) arayüzde açık göreli hız sönümü katsayı ne olursa olsun %0,5-0,6'da kaldı: bir alt
  adımda göreli hızın tamamından fazlasını alamaz, düzgün salınımda komşu iki bağın etkisi birbirini götürür (ikinci
  fark kalır). (2) XPBD'nin kapalı rijitlik sönümü (γ = β/h) yakınsamayı yavaşlatıp binayı yumuşattı (periyot 0,45 →
  1,43 s). (3) **Kütleye orantılı, zemine göre göreli hıza, yalnız bağlı cisimlere, ivmesi sınırlı** (0,5 m/s²,
  0,1 rad/s²): α = 1,2 ile %5,3, periyot değişmedi, düşen enkaz serbest.
- **Bağlı çiftte teması tamamen kapatmak yanlış:** yarı kırık bağla zemine 30 cm gömülen blok son lif kopunca 29 m/s
  fırladı. Bağlı çiftlerde 2 cm'yi aşan iç içe geçmeye temas (sürtünmesiz).
- **Ayrılma hızı tavanında "önceki ayrılma hızına izin" cırcır oldu** (1,5 → 11,5 → 29 m/s): iç içe geçme sürerken
  tavan yalnız sekme hızı / 2 m/s.
- **Yakalayıcı "bir karede 3 kat" kuralı iki karede biriken fırlamayı kaçırdı:** izleme (`test.watch`) buldu.

## Hızlandırmada öğrenilenler (10-02 gece, ölçülerek)
Gökdelenin göçüşü: fizik adımı 40-50 → 20-23 ms. Ölçü `--set test.perf=1` (her saniye adımın ortalaması ve en kötüsü,
CUDA Graph'la) ve `--set test.profile_at="s s"` (o anda bir kare çekirdek çekirdek; temas geçişinin parçaları ayrı).
- **İlk sezgi ölçülünce boş çıktı:** nokta başına double farkları ve kuaterniyon eylemsizliği dışarı almak (4080 double'ı
  float'ın 1/64'ü hızında koşar) ölçülebilir kazanç vermedi. Temas çekirdeğinin bölümlerine `clock64` sayacı koyunca
  yük göründü: sonraki yinelemelerin yeniden taramaları %56, ilk tarama %30, sürtünme %5. Alt adım başına 3,3 milyon
  örnek testinin onda biri karşı kutudaydı, onların %90'ı gerçekten iç içe (enkaz yığını).
- **Sonucu değiştiren kestirme kullanılamaz:** temas yinelemesi 4 → 1, adım 16 ms; ama gökdelen 7'de hiç yıkılmadı.
- **Çekirdek hesapla değil beklemeyle sınırlı:** `__launch_bounds__(128, 4)` (166 → 128 yazmaç, biraz taşma) %20-25
  kazandırdı; SM başına blok sayısını 2-3'e kısmak (arayüzlere yer açmak için) %40 kötüleştirdi; uzak örnek gruplarını
  atlamak testlerin %90'ını attı ama süreyi yalnız ~%8 kısalttı. Kazandıranlar bekleme zincirini paylaştıranlar: dar faz
  ayrı (bir şerit bir çift), çift başına 8 şerit (dört çift bir warp'ta; 4 şerit kötü, 16 aynı), yama başına ~%30.
- **Paylaşılan bellek her yerde kazandırmaz:** karşı cismin düzlemlerini ve örnek grubunu paylaşılan belleğe önceden
  yüklemek kazandırmadı; blok başına 23 KB doluluğu düşürüp küçük çiftleri yavaşlattı (19 → 31 ns/çift). Kaldırılıp
  düzlem döngüsü erken çıkışsız yapılınca 19 → 16 ns (erken çıkış sonra geri kondu: yıkım topunun 256 düzlemi var). İlk yinelemenin noktalarını saklayıp sonrakilerde yalnız onları
  çözmek büyük çiftte işe yaradı (44 → 30 ns), küçükte yavaşlattı: yalnız büyük çiftte.
- **En büyük tek adım boşta kalan kartı kullanmak:** arayüzler alt adım başına 26 küçük çekirdek, her biri birkaç SM'de;
  kartın geri kalanı bekliyordu. Temaslar arayüzlerle aynı anda (ayrı kolda), zemin temasları da arayüz kolunda:
  24,5-27 → 19-22 ms.
- **Paralelleştirince determinizmi yeniden sına; büyük sahnede:** iki yeni yarış önceden kapatıldı (dar faz bağın canlı
  sayısını arayüzler yazarken okuyordu → alt adım başındaki kopya `aliveC`; büyük çiftin noktaları atomik sayaçla
  kaydediliyordu → ballot ile örnek sırası). Gökdelen yine de iki koşuda farklı çıktı; kaydedilmiş kod da öyleydi:
  sürtünme çapası tablosunda bayat yuvayı yolda kapmak (bkz. `anchorSlot`). Yığın testi ve apartman bunu göstermiyordu.
  Düzeltmeden sonra gökdelen 7 dört koşuda 24. sn'ye kadar birebir; ama 35. sn'de iki koşu ayrıldı (çatı ötelemesi
  75,3 / 76,6 m). **Açık:** bir kaynak daha var, bulunmadı (iş toparlanırken bırakıldı).
- **Ölçüm gürültüsü ve betik:** aynı kod farklı saatlerde ±%10 oynadı (ayakta 10,9 ile 12,2 ms); küçük farkı tek
  koşudan okuma, A/B'yi aynı oturumda arka arkaya yap. A/B betiğim bir sürümü kendi üstüne kopyalayamayınca sessizce
  eskisini derleyip ölçtü ("fark yok" diye okundu); betik artık kopyalayamazsa duruyor.

## Büyüklükler, üç yapı (10-02 gece, temas hızlandırmasından sonra; `--set yapi=... --set deprem.magnitude=M`)
Ölçüm sarsıntı bittikten ~8 sn sonra, her biri bir koşu. En hızlı parça her koşuda yapının tepesinden serbest düşüşün
altında (apartman 17,5 / 17,8, gökdelen 43,6 / 43,8, ev 7,3 / 11,6 m/s). Bir önceki ölçümden (10-02 akşam, temas payı
düzeltmesinden sonra) farkı parantezde.

| Büyüklük | Apartman | Gökdelen | Ev |
|---|---|---|---|
| 3 · Zayıf | kırık 0 (25 çatlak) | ölçülmedi | ölçülmedi |
| 4 · Hafif | kırık 0 (965 çatlak; 974) | ölçülmedi | ölçülmedi |
| 5 · Orta | 130 kırık lif (116), öteleme %0,06 | kırık 0, öteleme %0,04 | kırık 0 |
| 6 · Kuvvetli | 815 (730), korkuluklar dökülür | kırık 0, öteleme %0,07 (kılcal çatlaklar) | 2 çatlak |
| 7 · Şiddetli | 2.772 (2.791), öteleme %0,60, ayakta | zemin kat gider, kule bütün olarak devrilir | 6 çatlak |
| 8 · Yıkıcı | göçer, çatı 13,5 m iner | ölçülmedi | göçer, çatı 2,4 m iner |
| 9 · Felaket | göçer, çatı 12,7 m iner (11,5) | göçer (çatı 96 m) | göçer, çatı 2,4 m iner |

Gökdelen sütunu çekirdek donatısız yapıldıktan sonra yeniden ölçüldü (5, 6, 7, 9): nitelik aynı. Dördüncü yapı,
sismik izolasyonlu kule: 5 ve 6 hasarsız, 7 324 kırık lif, 8 5.478 (birkaç cam), 9 17.219 (ayakta, boşluklarla); yukarıda.

## Büyüklükler (10-02, temas payı düzeltmesinden önce; `--scene yikim --clip sarsinti --still T --set deprem.magnitude=M`)
Dalga kaynakları: 3-4 Bayraklı, 5-6 İzmit, 7 Yarımca, 8-9 Antakya. Bina ölçümü sarsıntı bittikten ~8 sn sonra.

| Büyüklük | Tepe ivme | Süre (hız) | Bina | Kırık lif |
|---|---|---|---|---|
| 3 · Zayıf | 0,012 g | 3 sn (×2,2) | hasarsız, öteleme %0,01 | 0 (38 çatlak) |
| 4 · Hafif | 0,035 g | 5 sn (×1,8) | kılcal çatlaklar | 0 (1.076 çatlak) |
| 5 · Orta | 0,13 g | 8 sn (×1,4) | bağlarda çatlak, görünür hasar yok | 157 |
| 6 · Kuvvetli | 0,20 g | 14 sn (×1,2) | balkon korkulukları ve zemin kat dolgusunun bir kısmı dökülür | 896 |
| 7 · Şiddetli | 0,33 g | 26 sn | ağır hasar, öteleme %0,66, çerçeve ayakta | 3.024 |
| 8 · Yıkıcı | 0,70 g | 32 sn | yumuşak kat göçmesi (zemin kat 11. sn, bina 17. sn'de yerde), çatı 12,4 m iner | 22.805 |
| 9 · Felaket | 1,1 g | 45 sn | göçme (en güçlü kısım daha geç: zemin kat 14. sn, yerde ~21. sn), çatı 12,1 m iner; fırlayan parça diğer göçmeler kadar (yukarı en çok 5 m/s) | 22.811 |

Ayarlama turu (ölçülerek): ilk 7 (0,42 g) binayı yıkıyordu, 6 ile 7 arasında "ağır hasar ama ayakta" basamağı kalmıyordu
→ 0,33 g. İlk 9, Pazarcık dalgasıyla 4,2 g'ydi: göçüyordu ama parçalar yukarı 14 m/s fırlıyordu (yeni düzeltilen
fırlama dönmüş sanılırdı) → Antakya dalgası, 1,1 g. Pazarcık ×1 binayı yıkmadığı için (3,0 g yatay vektör, enerjisi kısa
periyotlarda) büyüklüklerde kullanılmadı.

## Kapanışta öğrenilenler (ölçülerek)
- **Pencerede karede iki fizik adımı 30 fps'e kilitledi:** fizik ~15 ms, görüntü 1 ms iken kare 32-35 ms. Gerçek zamanı
  yakalamaya çalışmak (adım biriktirmek) kare hızını yarıya indiriyor. Karede en çok bir adım + fizik ile görüntünün
  iki akışta üst üste binmesi (görüntü önce dönüşümleri okur, bir olay kaydeder, fizik adımı o olayı bekler): 55-75
  fps; fizik ağırlaştığında dünya biraz yavaşlar, kare hızı çökmez. Görüntü ekran kartını fizikle paylaşıyor: 16
  örnekte fizik 5 ms yavaşladı, 6 örnek denge.
- **Arayüz başına warp hızlandırmadı** (11,6 ms aynı): maliyet bellek beklemesi değil, 13 renk × 2 geçiş × 40 alt
  adımlık küçük çekirdek dizisi; her biri en uzun arayüzünün seri yürüyüşü kadar sürüyor. 13 renk boyamanın alt sınırı.
- **İç durumu göstermek denetlemektir:** donatı görünümünün ilk karesinde yerdeki tuğla korkuluk parçalarından demir
  çıkıyordu. Sebep `glueTouching`: parçaya özel donatı oranı (balkon 0,012) "iki taraftan büyüğü" kuralıyla değdiği
  tuğlaya geçiyordu; tuğla malzemesinde akma 0 olduğu için kuvvet taşımıyor ama Es·As ile tabanı 1,8 kat katılaştırıyor
  ve çatlama kuvvetini aynı oranda büyütüyordu. Hiçbir test ve klip bunu göstermemişti.
- **Bir hatayı düzeltmek eşikteki sonucu çevirebilir:** korkuluk düzeltmesi Pazarcık ×1'i göçmeden "ağır hasarla
  ayakta"ya taşıdı (×1,25 göçer). Fizikte her düzeltmeden sonra bütün tablo yeniden ölçülür; tek kayıt üstünden
  "model şunu yapar" denmez.
- **Taşıyıcıyı ölç, üstünde duranı değil:** "çatı" en üstteki parçaların ortalamasıydı, yani tuğla korkuluk; savrulan
  korkuluk, ayakta duran binada "çatı 9,7 m çöktü" diye okundu. Kare bakınca bina ayaktaydı. Ölçü artık taşıyıcı
  sistemin en üst parçalarında.

- **İç içe geçmeyi itmek bir şey fırlatmamalı (Özcan gördü, 10-02):** temas, derin iç içe geçmeyi alt adım başına
  `maxDepenetration·h` kadar itiyor, hız geçişi de ayrılma hızını o sınıra kırpıyordu: 2 m/s. Ağır parçada fark
  edilmez; 154 kg'lık korkuluk bloğu kirişe ~2 cm gömülüyor (bağlı çiftte temas ancak `BOND_OVERLAP` = 2 cm'den sonra
  devrede, tuğla bağı ezilirken blok o kadar batıyor), son lif kopunca bağ kalkıyor ve gömülme bir anda itiliyor:
  blok 3-4 m/s ile yukarı, dönerek. Tanıdaki iz: temas kuvveti 760-880 kN = 154 kg × 2 m/s / h (sınırın ta kendisi).
  0,5 m/s'de gömülme ~3 karede çözülür, oluşan hız gözle görülmez; testler (yığın, düşme, gömülme, enerji) ve bina
  sonuçları değişmedi. 1 m/s ara değeri Pazarcık ×1'i göçürdü (eşik kaotik), 0,25 ile 0,5 arasında fark küçük.

- **Yapay zekâ gürültü gidericisi fiziğin yanına sığmadı:** OptiX TEMPORAL_AOV 1080p'de 9,7 ms; fizik aynı kartta
  14,5 → 23 ms, pencere 40 fps, deprem gerçek zamanın üçte ikisi. Görüntü kusursuzdu ama "hız ve hissiyat" onaylıydı.
- **Katı cisim sahnesinde zamansal süzgeç kolay:** her parçanın bu ve önceki kare dönüşümü bilindiği için piksel başına
  hareket vektörü tam (cismin çerçevesine gir, eskisiyle çık); zemin depremle kaydığından onun kayması da düşülür. Geçmiş
  cisim kimliği, normal ve beklenen uzaklıkla denetleniyor. İlk ışın piksel merkezinden: kenarda kimlik kare kare
  zıplamasın.
- **Gölge geçerken süzgecin hafızası leke bırakır:** yıkılan binanın gölgesi zeminde kayarken aynı noktanın eski
  "güneşte" geçmişi kabul edildi (aynı cisim, aynı normal), gölgenin içinde aydınlık lekeler. Geçmiş, yeni karenin 3x3
  komşuluğunun ortalama ± 2 sapmasına kırpılıyor; büyük kırpmada hafıza kısalıyor (3 kare). Lekeler gitti.
- **Normal kılavuzunun uzayı:** ~~OptiX 9.1 başlığı (`OptixDenoiserGuideLayer`) AOV modelleri için normali **dünya**
  uzayında istiyor; sahne 1'in hazırlığı kamera uzayına çeviriyor (kaynak gösterilerek). Sahne 1'de doğrulanmadı.~~
  **2026-10-12'de çözüldü: kamera uzayı** (x sağa, y yukarı, z kameraya doğru). NVIDIA'nın üç kaynağından ikisi kamera
  diyor: OptiX 9.1 programlama rehberi (§16.1, model ayrımı yapmadan) ve `optixDenoiserInvoke` belgesi (zamansal
  modellerde 3B kamera uzayı); "dünya" diyen yalnız `optix_types.h`'taki yorum. Ölçüm (sahne 1, aynı girdi, 4096 örnekli
  referans): uzayı değiştirmek çıktıyı ortalama < 0,1/255 oynatıyor, rastgele normal 1,9/255 → kılavuz okunuyor ama
  dönmesi neredeyse fark etmiyor; seçimi belge yapar. Yıkım ve Laboratuvar dünya uzayı veriyordu, kamera uzayına çevrildi
  (Yıkım klibinde önce↔sonra ortalama 0,05/255).

## Deprem kayıtları (`assets/deprem/`, `src/yikim/quake.*`)
ESM (esm-db.eu, işlenmiş MP, hesapsız indirilebilir) ve AFAD TADAS (misafir girişi). İvme cm/s², hepsi ayrıştırılıp
PGA başlıkla karşılaştırıldı. Kullanılacaklar: **Pazarcık TK.4614** (2023, ESM MP, PGA 2,25 g; ham AFAD kaydında
6,8 cm/s² sabit kayma var, sürmek için ESM işlenmişi), Antakya TK.3123 (2023), Yarımca YPT ve İzmit TK.4101 (1999),
Bayraklı TK.3513 (2020, AFAD mp). Atıf: ESM (Mascandola ve diğ. 2026, doi 10.13127/esm.3) ve AFAD TADAS.
İki kez integre (yamuk), işlenmiş kayıtlarda hız ve yer değiştirme sonda sıfıra döner (Pazarcık PGD 0,5-0,6 m, PGV
0,93 m/s; Yarımca PGD 0,81 m). Zemin her karenin sonundaki konuma gider, alt adımlarda doğrusal; kayıt `deprem.offset`
saniyesinden, 1 sn'de yumuşak girişle başlar. Not: uygulamanın yazdığı PGA yatay vektör tepesi (Pazarcık 3,0 g;
bileşen tepesi 2,25 g).
