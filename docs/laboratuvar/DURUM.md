# DURUM · sahne 4 "Laboratuvar"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/laboratuvar/PLAN.md`'de. Eski "Tanecik" (insan) yolu
> `docs/tanecik/` ve git etiketi `tanecik-insan-arsiv`'de (arşiv).

## 🧊 Ek deney: Küp · 2026-10-03 öğleden sonra (otonom, 13:28 → ~16:00)

**Özcan'ın isteği:** *"Bir küp olacak. Bu küp patlayacak. Küp [...] taneciklerden oluşacak [...] Patlat diye tuş [...] ne
şiddette patlayacağını ve küpün tanecik sayısını çubukla seçeceğiz [...] milyon tanecikli küplere çıkabilirsin [...] pıt
diye küpe hafif tanecik dökülmesinden [...] büyük patlamaya kadar. Yine fizik ne derse o [...] sert, bir arada durup
nesne oluşturan tanecik. Yapışkan değil."* Malzeme: *"Renkli plastik, lego gibi [...] rubik küpler [...] onun gibi."*

**Ne yapıldı:** sekizinci deney **Küp** (tuş 8). 10 cm'lik bir küp, n×n×n küçük plastik küpten (zeka küpünün altı rengi,
küp başına rastgele), ortasında patlayıcı. Panel: **Küp sayısı** (4³ = 64 · 5³ · 6³ · 8³ · 10³ · 13³ · 16³ · 20³ · 25³ · 32³ ·
40³ · 50³ · 64³ · 80³ · 100³ = 1 milyon; aşağı yukarı ikişer katla, küp 25 mm'den 1 mm'ye), **Şiddet** (3 µg → 2 g TNT
eşdeğeri, logaritmik; altında yüzün ortasından çıkış hızı ve "dökülür / dağılır / patlar"), **Patlat** (Boşluk) ya da küpe
tıkla, ağır çekim, ince hesap. Masaüstü `Laboratuvar\11-15`.

**Çözücü (`cubes.h/.cu`, tanecik çözücüsünden ayrı):** her küçük küp katı cisim (konum + dönüş, eylemsizlik a²m/6).
- Kutu-kutu temas: ayırıcı eksenler (15), en sığ eksen yüzse gelen yüz başvuru yüzünün yanlarıyla kırpılır (ODE dBoxBox),
  kenar kenara bir nokta; en çok 4 nokta. Masa, fonun zemini, kavisi, duvarı köşelerle.
- Ekran kartında renklere bölünmüş Gauss-Seidel: hücreler köşegenin üçte biri (a/√3, bir hücreye en çok bir küp
  merkezi), 4³ = 64 renk, aynı renkten iki küp hiç değmez. İki evre: temaslar her alt adımda bir kez tam paralel bulunur,
  renk geçişleri yalnız bu kayıtları okur (konum düzeltmesi öteleme olduğu için derinlik birebir güncellenir).
- Ayrık itme (split impulses): iç içe geçme konumla, hız vermeden itilir; hız çözümü yaklaşmayı durdurur ve sekmeyi verir.
  Sürtünme nokta nokta Coulomb konisiyle. Son geçiş tek yönlü yukarı (şok yayılımı, Guendelman, Bridson ve Fedkiw
  2003): yığın durur. Masa son sözü söyler (her geçişte önce ve sonra). Öngörülü temas: bu adımda kapanacak boşluk da
  temas, CFL bölmesine gerek kalmaz. Hava direnci (1,5 a² ortalama izdüşüm), uyuma, temas eden yavaş küpte titreşim sönümü.
- Patlama: yön yön Gurney (o yöndeki koninin kütlesi / şarjın), içtekiler biraz daha hızlı ((r/R)^-0,5), hız log-normal
  dağılır, yön ve dönüş biraz saçılır; masaya bakan yarı masadan yansır (yere oturan patlamanın ayna şarjı); tek sayıda
  küpte ortadaki küp patlayıcının yeri. Yüzün ortası köşeden ~2,3 kat hızlı (testte 4,65 / 2,04 m/sn).

**Testler (`--scene laboratuvar --kup-test`, 6/6):**

| Test | Ölçülen | Ölçüt |
|---|---|---|
| Duruş: hepsi uyanık 8³ = 512 küp, 2 sn | en çok 0,09 mm kaydı, hepsi uyudu | < kenarın 1/10'u (1,2 mm) |
| Sekme: 25 mm küp 10 cm'den | 18,0 mm geri | e² h = 20,2 mm (%70-135) |
| Kayma: 1 m/sn | 16,9 cm, devrilmeden | v²/(2 μ g) = 17,0 cm |
| Yüz: eğik, dönerek düşer | 0,00° eğik, yüzü masada | < 2° |
| Hava: 1 mm küp 0,6 sn düşer | 3,11 m/sn | v_t tanh(gt/v_t) = 3,12 (havasız 5,89) |
| Patlama: 16³, 2 mg | Gurney 5,84 J, küplerde 5,32 J (yansıma dahil); yüz 4,65, köşe 2,04 m/sn; durulur | enerji %55-110, yüz > 1,5 köşe |

**Hız (RTX 4080):** canlı fizik kare başına 16³ patlamada en yoğun anda ~60-90 ms (2.000-2.500 küp birbirine ve masaya
değerken), uyudukça 0'a iner; 32³ patlama ~30 ms, 64³ ~64 ms, 100³ ~210 ms (5 kare/sn); yumuşak çöküş (yoğun yığın) 2-3
kat yavaş. Görüntü 1 milyon örnekli küple ~15-20 ms. İnce hesap dünya saniyesi başına 16³'te ~6 sn, 80³'te (512 bin) ~60 sn.
Büyük sayılarda canlı yavaş: panel "ince hesapla izle" der.

**Bulunan hatalar (düzeltildi):** sürtünme her noktada ayrı hesaplanıp toplanınca dört köşe kaymayı dört kez iptal
ediyordu (yığın kendini sarsıp dağıttı) · hızı konum farkından türetmek metrelerce uzakta float yuvarlamasında hızı %10
kaydırıyordu · Jacobi ortalaması yığında yakınsamıyordu (renkli Gauss-Seidel) · aynı hücredeki küpler aynı renkte
birbirinin yarım yazılmışını okuyordu · plastik malzeme ekran kartına yüklendikten sonra eklenmişti (küpler simsiyah) ·
`lalloc`'un sıfırlaması eski varsayılan akışta, büyüyen örnek dizisinde masayı ve fonu rastgele siliyordu (bütün
deneyleri etkileyebilirdi) · sayı ↔ çubuk dönüşümü iki listedeydi.

**Küp, NE DOĞRULANMADI:**
- Elle kullanmadım: otomatik test (deney seçimi, şiddet çubuğu, Patlat, ×1/100, ince hesap, sayı çubuğu, 125 bin küp) ve
  komut satırı kareleri. 1 milyon küpün ince hesabını uçtan uca uygulamada çalıştırmadım (komut satırında 512 bin, 1,2 sn,
  73 sn sürdü; 1 milyonda 2,5 sn kabaca 4-6 dk tahmini).
- Patlama modeli (Gurney yön yön, içtekiler (r/R)^-0,5, masa yansıması 0,6) gerçek bir patlayan küp kaydına karşı
  ölçülmedi; enerji ve yüz/köşe oranı yalnız modelin kendi içinde tutarlı.
- Kenar kenara temas tek nokta: dönmüş iki küpün kenarları birbirinden geçebilir (nadir; yığında görülmedi). Hızlı
  patlamada (≫ 10 m/sn) küçük küpler birbirinin içinden geçebilir (öngörülü temasın payı a'nın 1,2 katı).
- Canlıda büyük sayılar yavaş; canlı fiziği hızlandırmanın bir sonraki adımı bir küpün çiftlerini iş parçacıklarına bölmek
  olurdu (yapılmadı).

## 🏁 Sahne tamamlandı · 2026-10-03

Özcan: *"Kumda bilyeyi kaldıralım, içime sinmedi. Onun dışında güzel oldu. Bu sahneyi tamamladık."*

**Revize 3:** kumdaki çelik bilye kaldırıldı (dünyadan, çizimden, panelden, B tuşundan, ayarlardan, testten, belgelerden).
Kum yalnız huni: sürükle, akış, yükseklik. Çözücünün katı cisim (şekil eşleme) desteği yerinde, çözücü testleri
kullanıyor; sahnede kullanan kalmadı.

**Son hâl:** yedi deney (su dökme, bal, kum, su balonu, karpuz, balistik jel, süt damlası); canlı görüntü kararlı (revize
1); ağır çekim yalnız ağır çekim, ince hesap isteğe bağlı, jele saniyede bir çoklu atış (revize 2). Açık kalan, bilerek
dokunulmayan: ince dünyada jelin ilk atışta ~10 cm havalanması (revize 2'de, ayar Özcan'a soruldu, karar verilmedi),
süt tacının damlacık perdesi, jel boşluğu dar, karpuz parçaları yumuşak yüzey, balon masada (sabah raporunda).

## 🔧 Revize 2 · 2026-10-03 öğleden sonra: tıklanan yere bilye (revize 3'te kaldırıldı), jele çoklu atış, ağır çekim, ince hesap düğmesi

**Özcan'ın sınaması:** *"Tamam olmuş. Mükemmel değil ama yeterince iyi"*; iki revize: (1) kuma bilye hep aynı yere (solda)
düşüyor, yeri tıklayarak seçelim; ateşte de aynı mantık, 1 sn arayla birden çok atış (jele 6-7 kez); karpuz ve balonda
tek atış mantıklı. (2) 1/100 ağır çekime basınca hiçbir şey yapmadan süt saçılıyor, su dağılıyor: *"Sadece ağır çekim
olmalı."* Ayrıca ince hesap otomatik başlamasın, düğmeyle: *"İnce hesap yaptırmak opsiyonel olsun, daha tutarlı olur."*

**Ne yapıldı:**
- **Ağır çekim artık yalnız ağır çekim** (`LabWorld::advance`): canlı dünya her karede `1/60 × hız` saniyelik bir adımı
  her zamanki 20 alt adımla atıyordu; 1/100'de alt adım 8 µs'ye iniyordu. PBF'nin konum düzeltmesi o kısa süreye
  bölününce hız 100 kat büyüyor: duran dünya kendiliğinden patlıyordu. Şimdi alt adımın boyu hep aynı (1/1200 sn),
  ağır çekimde yalnız daha seyrek atılıyor; bir kare bir alt adımdan azını gerektiriyorsa görüntü önceki ve sonraki
  alt adımın arasında süzülüyor (`launchLerp`). Ölçüm (`--lab-test yavas`, duran dünya, 1/100'de 0,05 sn):

  | | en hızlı tanecik | yukarı fırlayan | gerçek hızda aynı süre |
  |---|---|---|---|
  | Süt, eski yol | 3,10 m/sn | 7.367 | 0,195 m/sn, 0 |
  | Süt, yeni | **0,195 m/sn** | **0** | |
  | Su, eski yol | 22,1 m/sn | 21.868 | 0,131 m/sn, 0 |
  | Su, yeni | **0,147 m/sn** | **0** | |

- **İnce hesap isteğe bağlı:** tetik yalnız kaba dünyada oynar (sol üstte **Kaba hesap**); panelde **İnce hesap yap**
  (**I**) aynı girdilerle ince dünyayı başlatır, bir daha basınca durdurur. Bitince bir kez gerçek hızda oynar (o sırada
  kayıt izlenmiyorsa). İnce kayıttan **R** / **Kaba hesaba dön** kaba dünyaya olduğu yerden döner (kayıt zaman çubuğunda
  kalır); kaba dünyada **R** yeniden kurar.
- **Kum: tıklanan yere bilye.** Panelde **Sol tık: Huniyi gezdir / Bilye bırak** (**B** değiştirir). Bilye kipinde sol
  tık, tıklanan noktanın (kum, masa; havaya ya da huniye tıklanırsa masadaki iz) üstünden 28 cm'den bir bilye bırakır,
  saniyede en çok bir. Altı bilye var, hepsi bitince en eskisi yeniden kullanılır. Eskiden B, bilyeyi huninin altına
  bırakıyordu ("hep solda" bu).
- **Jel: saniyede bir, istediğin kadar atış** (tıkla ya da Boşluk). Karpuz, balon, dökme, damla tek tetik. İnce hesap
  bütün atışları sırayla yeniden oynatır: kayıt her atışın ardından 0,3 sn boyunca her adımı (saniyede 2000), arada
  saniyede 120 anı saklar; bellek yetmezse yoğun kısım seyrekleşir (her 2., 3. adım), sonra son kırpılır. Atışlar
  arasındaki 0,8 sn'den uzun bekleme 0,8 sn'ye kısalır (`jel.kayit_ara_en_cok`): blok o arada yalnız durur; böylece 7
  atış ~5 sn dünya zamanı, ince hesabı 1-2 dk (uygulamada dünya saniyesi 10-21 sn sürüyor; kısalmasa dakikalar). Ölçüm: 3 atış, 1,21 sn dünya zamanı, 2421 an, 2,8 GB, 11,9 sn
  hesap (`--seq --deney jel --tetik --kayit --atis 3 --atis_ara 0.4`).
- Otomatik test yeni akışla: her tetikten sonra İnce hesap yap düğmesine gerçek tıklama; kumda iki bilye (üçüncü tık
  erken: alınmadı), jele üç atış ve ince hesabı, sütte ve balda dokunmadan ×1/100, ince kayıttan kaba hesaba dönüş.
  Testler: laboratuvar 3/3 (balon, kum 32,7°, yavaş), otomatik test geçti.

**Revize 2'de görülen, dokunulmayan:** ince dünyada jel bloğu ilk 9 mm atışında ~10 cm havalanıp dönüyor (tek atışta da,
gecenin kaydında da öyle; yeni değil), kaba dünyada yalnız sarsılıyor. Bloğumuz küçük (24×10×10 cm, ~2,5 kg; standart
blok ~9 kg): aynı itiş ona dört kat hız, ~16 kat yükseklik verir, yani akla yakın olabilir; gerçek kayda karşı
ölçülmedi. Çoklu atışta sonraki atışlar kaba dünyadaki bloğun yerine nişanlandığı için ince dünyada havadaki bloğun
başka yerine değebilir ya da ıskalayabilir (aynı ışın, ayrışan dünya).

**Revize 2, NE DOĞRULANMADI:**
- Elle kullanmadım: otomatik test sanal fare ve tuşlarla, ekranlar ve günlük. Bilye bırakma noktasının tıklanan yere
  denk düştüğüne günlükteki tıklama noktası ve ekranla baktım (iki bilye, ikisi de tıklanan yerde).
- 6-7 atışlık ince hesabı uçtan uca çalıştırmadım (3 atış denendi); süresi ve belleği 3 atıştan kestirildi.
- Ağır çekimde süzülme yalnız taneciklerde: taşınan cam kapların (beher) duruşu alt adım aralarında atlar (0,8 ms'lik
  dönüş, gözle seçilmez sandım; bakılmadı).
- ×1/10'da (karede 2 alt adım) süzülme yok, gerekmiyor sandım; 20'den az alt adımlı bir deneyde gerekebilir.

## 🔧 Revize 1 · 2026-10-03 öğlen: görüntü kararlılığı

**Özcan'ın sınaması:** *"Fizik olmuş, gayet güzel. Ancak önemli bir sorun görüntü kalitesinde [...] sanki suyun
içindeymiş gibi hem bulanıklık var hem böyle çok kıpraşıyor [...] bi o yana bi bu yana dalgalanıyor gibi."*

**Neden:** canlı görüntü her karede sıfırdan 4 örnekle çiziliyor, yapay zekâ gürültü gidericisi her kareyi ayrı
temizliyordu. 4 örnekli gürültüyü her karede başka biçimde düzleştirdiği için cam ve sıvı kenarları, masa ile fon arası
çizgi kareden kareye kayıyordu (dalgalanma); düzleştirme ayrıntıyı da siliyordu (bulanıklık). Dünyayı dondurmak suda
ölçüyü değiştirmedi: kaynak fizik değil, çizimdi. Üstüne 2 mm diyafram odak dışını bulanıklaştırıyordu.

**Ne yapıldı:**
- **Zamansal birikim** (`lab_kernels.cu` `kTemporal`): her kare öncekilerle harmanlanır; geçmiş, kameranın önceki
  konumundan geri izlenerek bulunur (her pikselin ilk değdiği nokta), bu karenin komşuluğuna (YCoCg, ort ± 2,5 sapma)
  sıkıştırılır. Yeni karenin payı en az 0,1; hiçbir şey değişmiyorsa (dondur) düz ortalama, görüntü yakınsar.
- **Geçmiş ancak aynı yüzeyi gösteriyorsa kullanılır:** her piksel için sabit bir ışın (piksel ortasından, camda ve
  sıvıda hep kırılarak) ilk değdiği noktayı ve ilerlediği yolu yazar; önceki karede orada başka nokta ya da başka
  uzunluk varsa o piksel baştan başlar. Bu olmadan dökülen su akışı fonla karışıp silikleşiyordu (masaüstündeki
  karşılaştırmada "ara deneme"). Kamera kayarken camın ardındaki görüntü de kayar: uzunluk payı kameranın aldığı yolla
  genişler.
- **Gürültü gidericinin kılavuzları** (albedo, normal) aynı sabit ışından: eskiden örneğin rastgele seçtiği yoldan
  (camda bir yansıyan bir geçen) geliyordu, giderici camı her karede başka çiziyordu.
- **Aynadaki ışık kutusu tam sayılır:** kameradan gelen ışın havadan cama ya da sıvıya değdiğinde, yansıma yönündeki
  ışık kutusu F ağırlığıyla her örnekte eklenir (yansıyan örnek onu bir daha saymaz; yansızdır: 1024 örnekli
  referansta ortalama 117,93 → 117,94). Eskiden 20 örnekte bir yansıyan ışın bulduğu için tabak kenarındaki parlaklık
  karıncalanıyordu.
- Titreşim (8 bit bantlanmaya karşı ±1/255) artık sabit desen: her kare değişen titreşim tüm resimde ince kıpırtıydı.
- **Diyafram varsayılan 0** (her şey keskin); `kamera.diyafram = 0.002` eski sığ alan derinliğini geri getirir.

**Ölçüm** (`--kararlilik`: sabit kamera, canlı pencere gibi 40 kare, ısınma sonrası 32; "dalgalanma" 8×8 blokların
parlaklığının kareler boyunca sapması, ince gren blokta ortalanır, kayan leke ortalanmaz; değerler 0-1 ekran aralığında):

| | önce (dalgalanma %99) | sonra | önce (kareden kareye) | sonra |
|---|---|---|---|---|
| Su, durgun | 0,0109 | **0,0031** | 0,0014 | **0,0003** |
| Bal, akıyor | 0,0194 | **0,0059** | 0,0029 | **0,0009** |
| Bal, dünya donuk | | 0,0040 | | 0,0004 |

"Önce" sütunu sabit titreşimle ölçüldü; gecenin sürümünde (titreşim her kare değişiyordu) kareden kareye fark su
0,0030, bal 0,0042 idi: **su 10 kat, bal 4,7 kat sakin**. Bardağın içi referansa göre (1024 örnek) ~%3 koyu
(sıkıştırmanın payı; gama 1,5'te %4). Kare süresi değişmedi (otomatik test: bal 31,7 → 34,9 fps, kum 29,1 → 33,6, jel
kaydı 33,3 → 45,3). Karşılaştırma resmi: masaüstü `Laboratuvar\10 goruntu kararliligi (once - sonra).png`.

**Denenip bırakılan:** yeni karenin payı 0,05 (ölçü az değişti, iz riski arttı) · sıkıştırmasız (kamera kayarken masa
ile fon arası çizgi sürükleniyordu) · gama 1,5 (cam %4 koyu, daha çok oynuyor).

**Revize 1, NE DOĞRULANMADI:**
- Kıpraşmayı gözle izleyemem: kareler ve kendi ölçüm aracım. Asıl hakem Özcan'ın exe'de bakması.
- Kamera hareketi yalnız yana kayma ile (karede 2 mm) denendi; fareyle dönme ve hızlı WASD ölçülmedi. Kamera
  hareket ederken camın içi biraz grenli (geçmiş kısmen bırakılıyor), durunca toplanır.
- Kamera hareket ederken camın ardında sıvı da hareket ediyorsa yalnız komşuluk sıkıştırması korur: hafif iz olabilir,
  denenmedi.
- Kum ve kayıtların oynatması için ayrı kararlılık ölçümü yok; yalnız otomatik test ekranlarına baktım (iz görmedim).
- Görüntü 1920×1080 çizilip 2560×1440 pencereye büyütülüyor (Yıkım'da da öyle). Daha keskin isteyen
  `display.width = 2560`, `display.height = 1440` yapabilir; maliyeti ölçülmedi (kabaca 1,8 kat çizim).

## ☀️ Sabah raporu · 2026-10-03 (gece 01:10'dan, Özcan uyurken otonom)

**Ne yapıldı:** sahne 4 "Laboratuvar" baştan kuruldu ve uygulamada galeride 04. Bir ağır çekim fotoğraf masası,
**yedi deney**: su dökme, bal, kum, su balonu, karpuz, balistik jel, süt damlası. Hızlı olanlar **kayıt** (tetik
kaba dünyada hemen oynar, ince dünya arka planda hesaplanır, hemen izlenir, bitince gerçek hızda oynar, sonra zaman
çubuğu, ×1/10, ×1/100, dondur, WASD), bal ve kum **canlı**. Video yok: saklanan fizik, her açıdan canlı çiziliyor.
Gürültü giderici, alan derinliği (otomatik odak), README EN + TR, gösteri rehberleri, galeri resmi ve testler.

**Nasıl açılır:** `C:\dev\pc_graphics\build\pc_graphics.exe` (çift tık) → galeride 04 Laboratuvar → Enter. Ya da
`build\pc_graphics.exe --scene laboratuvar`. Tuşlar ekranda F1, ayrıntı `GOSTERI.md`. Deney paneldeki **Deney**
satırından (ya da 1-7). Kareler masaüstünde `Laboratuvar` klasöründe (00 galeri karesi, 01-09 deneyler).

**İlk bakılacaklar:** Su balonu (Boşluk ya da balona tıkla; kayıt bitince Zaman'ı başa çek, ×1/100, P) · Su (yükseklik
çubuğunu en yükseğe al, dök) · Bal (fareyle kaşığı gezdir) · Kum (huniyi behere götür) · Karpuz (Tüfek / Tabanca).

## Deneyler

| Deney | Kip | Ne oluyor | Durum |
|---|---|---|---|
| Su | kayıt | beher eğilip bardağa döküyor; yükseklik 12-45 cm | ince 1,5 mm (113 bin su), saniyenin 1/600'ü; düzgün cam şerit, sıçrar, taşar, masada birikir |
| Bal | canlı | kaşıktan cam tabağa; fareyle gezdir, akış ve yükseklik | kesintisiz kehribar ip, tabakta katlanıp kıvrılır, göle döner |
| Kum | canlı | huniden masaya ve behere; fareyle gezdir | yığılma açısı **32,5°** (kuru kum 30-35°); behere koni hâlinde dolar |
| Su balonu | kayıt | masadaki balona iğne (tıkla ya da Boşluk) | 10 ms'de delik, 20-30 ms'de lastik soyulurken su balon biçiminde, sonra çöker |
| Karpuz | kayıt | tabanca ya da tüfekle (5,56 → 7,62×51 → .50 BMG, gerçek yükler) | tüfek: ~600 J, birkaç ms şişer, kabuk taç gibi açılır, dağılır; tabanca: açar, kaideden düşürür |
| Jel | kayıt | 24×10×10 cm balistik jel, tabancayla (.22 → 9 mm → .50 AE) | 9 mm: 24,5 cm'de 329 J; yol boyunca boşluk tüpü, blok sarsılır ve biraz zıplar |
| Süt damlası | kayıt | petri kabında 3 mm süt, 5 mm damla 5-50 cm'den | 0,4 mm tanecik (134 bin), saniyenin 1/5000'i; taç açılır, damlacık saçar |
| Küp | kayıt | 10 cm küp, 64 → 1 milyon plastik küp, ortasında 3 µg → 2 g TNT | katı küpler: yüzüne düşer, takla atar, seker, kayar; pıt diye dökülmeden büyük patlamaya |

**Kayıt düzeni:** ince dünya bir işçi iş parçacığında kendi CUDA akışıyla; her an ekran kartında (16 bit konum + çizim
kodu, tanecik başına 8 bayt); zaman çubuğu hesaplanan kısmı gösterir. Oynatma yalnız çizim, anlar arası doğrusal ara
doldurma.

## Ölçümler

- **Testler (bu gecenin son koşusu):** T1 çözücü testleri 9/9 (baraj Martin-Moyce RMS 0,069, hacim, balon, tekrar bit
  bit aynı), yüzey 8/8, silah 6/6, laboratuvar 2/2 (`--lab-test`: balon masada sağlam + iğnede patlar; kum 32,5-32,7°).
- **Kum:** 6 sn dökülen 10.439 tane, yığın 25,3 mm, taban yarıçapı (%95) 39,7 mm → 32,5°. Yol: özdeş Jacobi payıyla
  sürtünme ~0° (sıvı gibi yayıldı) → kök payla 14,5° → 4 geçişle 19° → hız düzeyinde durgunluk (1,2 cm/sn altı) 32,5°.
  Eşik 2 cm/sn'de 38,9°, 5 cm/sn'de 59,7°.
- **Balon:** masada 2 sn, 31.256 bağdan 2'si koptu, zar dışında su 0; iğneden 5 ms sonra bağların %45'i koptu.
- **İnce hesap süreleri (komut satırı / uygulamada görüntüyle birlikte):** su 4,5 sn dünya zamanı ~7 / ~16 sn; balon
  1,2 sn ~6 / ~14 sn (oturma 4,8 sn); karpuz 1,2 sn ~6 / ~11 sn; jel 0,3 sn ~3 sn; süt damlası 0,06 sn ~1 sn (oturma 6 sn).
- **Uygulama (otomatik test, 2560×1440, 1080p iç çizim, 4 örnek + OptiX yapay zekâ gürültü giderici):** su canlı 39 fps,
  önizleme + ince hesap 21-23, bal 31, kum 29, balon kaydı 14-23, karpuz 20-30, jel 28-38, süt 15-24 fps. Bütün uygulama
  testi (dört sahne) geçti; her sahne kapanınca ekran kartı belleği geri geliyor (14.857 / 16.376 MB).

## Kararlar ve gerekçeler

- **MPM yazılmadı (karar 6 esnetildi):** bütün deneyler T1'in testli PBF/XPBD çözücüsüyle kuruldu, zorlar ölçümle
  çözüldü. PBF'in en zayıf olduğu yer (bal ipi: gerilmeye direnen gerilim yok) viskoelastik yaylarla kapandı.
- **Balon masada, asılı değil:** asılıyken hafif lastik suyun ağırlığını düğüme zamanında taşıyamadı (24 geçişte de).
- **Zarın bağ geçişi 12 (ince 20):** T1 balonu 7 mm'de geçiyordu, aynı ayarla 3 mm'de %62,7 su dışarı (XPBD sertliği
  çözünürlüğe ve geçişe bağlı). Ön gerilim (dinlenme 0,85) oturmada yavaşça verilir (bir anda: 4.133 bağ koptu).
- **Çatlak ucu (yalnız iğneden sonra):** bağı kopmuş taneciğin komşu bağı kopma şekil değiştirmesinin 0,4'ünde kopar
  (kaba kafes uçta yoğunlaşan gerilimi çözemez; kuralsız delik %4'te durdu). Dinlenmede kapalı.
- **Su:** XSPH 0,05 + Akinci yapışma 20 + yüzey gerilimi 3 (olmadan akış erişteye bölünüyordu, dört varyant kıyaslandı).
  Dudak yüksekliğe göre geride (45 cm'den bardağı ıskalıyordu).
- **Bal:** Shepard XSPH + alt adımda 12 geçiş + tutulan meme + viskoelastik yaylar (Clavet 2005: her yeni katman öncekine
  bağlanır, dinlenme boyu 1 sn'de uzunluğu izler, 2,5 sn sonra yay kalkar). Önceki denemeler: yağ gibi yayılma, katmanlara
  ayrılma, Rayleigh-Plateau damlaları.
- **Kum:** tane fazı (kendi grubuyla çarpışan), sürtünme kök payla, 4 geçiş, hız düzeyinde durgunluk; görüntüde yüzey +
  0,35 mm'lik tane dokusu + havada yalnız ince görüntü taneleri.
- **Karpuz:** kabuk iki katman (yeşil deri ≥ 2 tanecik, açık iç kabuk; tek yüzeyde çizilir), gerilme modülü kabuk 15 MPa,
  et 1 MPa (uyum 1/(E·aralık)), bağlarda log-normal kusur σ 0,6, yuvalı kaide, geçici boşluk payı 0,15 (0,5'te 6 ms'de
  konfetiye dönüyordu; gözle ayar, ölçüm değil), sıkı yüzey çekirdeği (keskin kenar).
- **Jel:** E 0,3 MPa, kopma %150, boşluk payı 0,3 (0,8'de blok masadan sıçradı).
- **Süt:** yapışma 40, gerilim 5; cam kapların iç yuvarlatması ayarlanabilir, sıvı dolumu duvara 0,9 aralıktan yakın
  noktaları eler (petrinin 4 mm yuvarlatması 3 mm sütü kesiyordu: süt camın içinde doğup fırlıyordu).
- **Işık ve kamera:** fonun önünde kadrajdan büyük, kenarı yumuşak düşen panel (sıvı fotoğrafçılığının arka ışığı);
  cam ve sıvı tam Fresnel ve kırılmayla, bardaktaki su ile cam arası tek arayüz; 2 mm diyafram, odak ekran ortasında
  (revize 1'de diyafram varsayılanı 0: her şey keskin).

## Bu gece bulunan hatalar (düzeltildi)

- **Kinematik hedef yarışı:** beherin hedefleri varsayılan akışta hesaplanıyordu, sürüş engellemeyen akışta; ilk kare
  başlatılmamış hedef okudu, canlı önizlemede su fırladı. Artık çözücünün akışında.
- **Kayıtta tetik sırası:** damla gibi tanecik ekleyen tetik çizilen listeden sonra yapılıyordu (damla kayda girmiyor,
  tampon taşıyordu). Artık önce tetik.
- **Deneyler arası ayar mirası:** iğne çözücünün çatlak ucu ayarını açıyor, sonraki deneyin dünyası devralıyordu; kaba
  karpuz kaidede kendiliğinden parçalanıp çöktü (komut satırında her koşu yeni süreç olduğu için görünmedi). Her
  kurulumda ayarlar sıfırlanıyor.
- **Eski varsayılan akış yükleme yarışı:** çalışma anı yüklemeleri (`cudaMemcpy`, `cudaMemset`) çözücünün engellemeyen
  akışındaki çekirdekten önce bitmeyebiliyordu; GPU meşgulken (uygulama çizerken) atışın işaretleri sıfır okundu, ince
  karpuzda hiç bağ kopmadı. Yüklemelerden sonra eski akış bekleniyor.
- **Akıtıcı eleme tohumu:** bir red bütün katmanı reddettiriyordu, akış beklenenin 1/8'iydi.
- **Komut satırında odak:** `--cam/--look` verilince odak eski uzaklıkta kalıyordu (bulanık kareler).

## Açık / bilinen sınırlar

- **Süt tacı** ince duvarlı değil, damlacık perdesi gibi (gerçek taç duvarı ~0,2 mm, tanecik 0,4 mm).
- **Jel:** geçici boşluk dar (~1 cm; gerçek 9 mm boşluğu 5-6 cm). Basınçla açılıp kapanan bir boşluk modeli gerek.
- **Bal ipinin** yüzeyi biraz boğumlu (1-3 tanecik kalınlığında); kıvrılma sıklığı (Ribe ölçeklemesi) ölçülmedi. Yaylar
  kayan noktalı atomikle: bal birebir tekrar etmez.
- **Karpuz parçaları** yeniden kurulan yüzeyle çiziliyor, keskin kırık yüzey değil; tüfekte parçalar kadrajdan çıkar.
- **Su balonu** yalnız masada; ince balonda iğne, kaba balondaki noktanın en yakın zar taneciğine batar (biçimler biraz
  farklı: ince balon daha az sarkar, 17 mm kayma ölçüldü).
- **Kum akışı** düşerken kümecikli (fizik tanesi 2,5 mm).

## NE DOĞRULANMADI

- Uygulamayı insan gibi elle kullanmadım; doğrulama otomatik testle (sanal fare ve tuşlar: deney seçimi, düğmeler,
  balona ve karpuza **fareyle tıklama**, zaman çubuğu, hız, kaşığı sürükleme) ve komut satırı kareleriyle.
- Bal ve kum dakikalarca açık kalınca tanecik kapasitesine (150 bin ek) varılır; o noktada akış durur ve son resim
  kalır: denenmedi.
- Art arda çok sayıda kayıt (bellek) ve F5 ile ayarları yeniden yükleme denenmedi. İngilizce arayüz otomatik testte
  görüldü (`--lang en`: Laboratory, Fine recording, Back to live…).
- Süt tacının ve jel boşluğunun gerçek ağır çekim kayıtlarıyla sayısal kıyası yapılmadı (yalnız göz).

## Komut satırı

```
build\pc_graphics.exe --scene laboratuvar --still --deney su --tetik --kayit --t 1.4 --spp 64 --out captures\x.png
build\pc_graphics.exe --scene laboratuvar --seq --deney balon --tetik --kayit --t0 0 --t1 0.07 --kare 8 --out captures\x
build\pc_graphics.exe --scene laboratuvar --lab-test [balon|kum|yavas]
build\pc_graphics.exe --scene laboratuvar --test [baraj|balon|...]     (T1 çözücü testleri)
build\pc_graphics.exe --autotest-only laboratuvar
build\pc_graphics.exe --scene laboratuvar --kararlilik --deney su --kare 40 [--dondur] [--tetik] [--kaydir 0.002] --out captures\k
```
`--kararlilik` canlı pencere gibi kare kare çizer, piksel ve 8×8 blok sapmasını, kareden kareye farkı ve kare süresini
yazar; `<out>_isi.png` sapma haritası, `<out>_kare.png` son kare. `--dondur` dünyayı durdurur, `--kaydir` kamerayı her
karede o kadar metre yana kaydırır. Görüntü anahtarları: `goruntu.zamansal`, `goruntu.zamansal_alfa`,
`goruntu.zamansal_gama`, `goruntu.gurultu_giderici`, `kamera.diyafram`.
Ortak: `--width --height --cam "x y z" --look "x y z" --set anahtar=değer --yukseklik --akis --guc`; jelde
`--atis N --atis_ara S` N atış (her biri biraz yukarı ve geride), S saniye arayla.
`LAB_TANI=1` oturma ve akıtıcı tanısı yazar. Deney adları: su, bal, kum, balon, karpuz, jel, damla.
