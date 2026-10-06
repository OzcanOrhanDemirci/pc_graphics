# DURUM · sahne 8 "Denizaltı"

> Tek kaynak durum panosu. Plan `docs/denizalti/PLAN.md`, kaynaklar `docs/denizalti/REFERANS.md`. En yeni en üstte.
> Otonom görev (Özcan, 2026-10-06 sabahı): soru sormadan, bitene kadar; kareler SendUserFile ile.
> **İlk yükleme bütçesi (Özcan, 10-06 öğle):** önden hesaplama + yükleme tavanı 6 dk, ~5 dk kabul; daha kısa sürede en
> yüksek kaliteye varılıyorsa daha kısa.

> **Canlı kare bütçesi (Özcan, 10-06 öğleden sonra):** 1440p High, SSAA kapalı 30-40 FPS'e kadar (kare ~25-33 ms).

**Özcan için (10-06 akşamüstü):** sahne galeride 08. İlk açılış ~5 dk (yükleme ekranında ilerlemesiyle), sonra
önbellekten. Bakılacak yerler `GOSTERI.md` §08'de: dalış istasyonu, harita masası (Ege seyir haritası, kalemle rota),
sancak panoları (döner şalterler, devre etiketleri), kafesli kırmızı lambalar, telsiz odasının kapısı. Görseller
`docs/images/denizalti-*.jpg`.

**Durum: görev kapandı (10-06 akşamüstü).** Bağımsız gözden geçirmenin bulguları düzeltildi, son pişirme ve ölçümler
aşağıda. Açık kalanlar "D7 (2)" sonunda.

## D7 (2) · Bağımsız gözden geçirme ve kapanış (2026-10-06 akşamüstü)

Bir ajan kodu (a4ba730'dan bu yana) ve kareleri kendi gözüyle denetledi (12 kod, 13 görsel bulgu). Düzeltilenler:

- **Panolar yanlış yükseklikteydi (yüksek):** `hullBoard` açıyı mantarın yarıçapından alıyordu, pano kendi yarıçapında
  11-20 cm alçak duruyordu; vent kolları Christmas tree kutusuna giriyordu. Doğru yükseklikte ise **gövdedeki boru
  şeridiyle çakıştılar**: kesit düzleminde hesap (her pano × 8 boru + 4 kablo borusu) ağacın içinden 4 boru ve 2 kablo
  borusunun geçtiğini gösterdi. Panolar şeridin önüne alındı (arkaları 25-27 cm içeride), postalara **braketlerle**
  bağlandı (braketler boruların geçtiği yerden kaçar), dalış istasyonu panosu 1,85 m'de biter (göstergeler oturan
  dümencinin göz hizasında), dolapların üst-arka pahı borular arkadan geçecek kadar büyüdü. Hesap: hepsi temiz.
- **Bloklu pus halesi:** 4 cm'lik pişmiş pus hücreleri üç doğrusal okununca lamba konisinin kenarı eksenlere hizalı
  basamaklar veriyordu. Pus hacmi **kübik B-spline** okunur (8 süzgeçli okuma, froxel geçişinde).
- **Temas gölgelerinde texel basamağı** (cetvel, kalem, pergel): ışık haritası Işık High'da, texel ekranda 2 pikselden
  büyükse kübik B-spline okunur; geçiş bandında iki okuma karışır (eşikte anahtarlama kıpırdamasın). +1 ms (1x).
- **Toz** harita, laminat, cetvel ve kadranlarda yok (gürültü bulut gibi okunuyordu).
- **Şablonlar** yalnız harflerde çizilir (örnek başına alfa testi) ve **fırında engel değil**: dörtgen engel olunca
  altındaki boyayı kapkara pişiriyordu, dörtgen saydamlaşınca kara bir levha göründü (bulundu, düzeltildi).
- **Pencereler koyu cam:** MAT_GLASS artık atılmıyor; arkası karanlık, yüzünde odanın yansıması.
- Kalem cetvelin içindeydi (uzaklaştı); menteşeli pergel, uçları kâğıtta; kulplu kupa, haritanın dışında; panjur
  çubukları çerçevenin derinliğinde; manifold etiketi tabanında; **aynı devre adı iki kez yok** (fazlası "SPARE",
  "… No.2").
- **Harita:** basılı alan iç çerçevede biter; dakika etiketleri (derece gerçek bir kıyıyı adlandırıyordu, çizim o kıyı
  değil); sapma 1968 Ege'sine göre 2°30'E; düz alfa (kenarlar griye çalmıyor, mip'ler kâğıtla ortalanır); derinlik
  renkleri yumuşak; atlas payı 24 px.
- **Fırın:** yoklamalara ayrı parlaklık sınırı (1,0: aynanın gördüğü yüzey ateş böceği değil); yoklama başlatması
  başına yol eski sınırında (yavaş kartta sürücü bekçisi); albedo tablosu önbellek anahtarında; stensiller engel değil
  (sürüm 13).
- 2×2 SSAA yedeğinde pus ekran okuması hedefin boyuyla; galeri listesi çok küçük pencerede taban yükseklikle.
- **Yeni içerik:** trim manifoldunun alt sırasında pirinç vana künyeleri (halka ile çarka asılı), dalış istasyonunda
  lastik paspaslar, baş perdede ses gücüyle çalışan telefon (ahize, seçici, "1JV" levhası, kordon).

**Ölçümler (son pişirme, 512 örnek):**
- Taban ağı 7,32 milyon üçgen; ışık atlası 8192×10416, 62,2 milyon texel. **Pişirme 305 sn, ilk açılış ~5 dk 10 sn.**
- **Işığın gürültüsü** (256 ve 512 örnekli iki pişirme, iç içe örnekler: farkın sapması 512'nin kendi gürültüsüdür;
  dört görünüm, 16x, 8 bit çıktı): sapma **0,22-0,59 / 255**, %99'luk dilim ≤ 1,7 seviye, 8×8 blok lekeliliği
  0,09-0,50 / 255, yanlılık ~0. Bir renk adımının altında: görünmez.
- Kare süresi 1440p (başlangıç görünümü, Grafik/Işık/Efekt High):

| Tess | 1x | 2x | 4x | 8x | 16x |
|---|---|---|---|---|---|
| Low | 8,2 | 9,0 | 13,9 | 24,0 | 35,2 |
| Medium | 9,1 | 9,9 | 15,6 | 27,5 | 39,5 |
| High | **13,8** | 16,9 | **33,3** | 75,2 | 130,5 |

  Grafik/Işık/Efekt seviyeleri (High tess, 1x): hepsi High 11,9; Grafik Low 8,2; Işık Low 11,0; hepsi Low 7,2 ms
  (D7 (2)'den önce ölçüldü).
- Canlı uygulama (1440p 4x High): **31 FPS, 32,7 ms**, kart belleği 5,4 / 16 GB. Özcan'ın canlı bütçesi (1440p High
  1x 25-33 ms) içinde: 13,8 ms.
- Oda çatlak testi: **0 piksel**. Kararlılık (canlı 4x / referans 16x): başlangıç yolu piksel 0,00068 (blok 0,00017),
  FLIP 0,0043; yakın yol 0,00039 (blok 0,00015), FLIP 0,0024. Ada tabanının (0,001) altında, ama D7 (1)'den biraz
  yüksek: ısı haritasında kıpırtı hareketli ince ve parlak kenarlarda (kafesli vantilatörün telleri, şalter sıraları,
  doğru yerine çıkan Christmas tree'nin yeşil lambaları); 4x'te ince geometrinin kenar kırılması.

**Açık kalanlar (bilerek):**
- Ateş böceği sınırı (0,06) lambaya bitişik yüzeylerin sektirdiği ışığı da biraz kırpar (enerji yanlılığı, ölçülmedi).
- Çıkartma tablosu 256 kayıt (şu an altında; GPU tarafında kimlik sıkıştırılır, kayıt sayısı payı ölçülmedi).
- Alüminyum hava panosunda kalan hafif yansıma dikişi (yoklama sınırı).
- Galeri listesinin 1440p'deki görünümü ölçülmedi (bu oturumda monitör 1280×720 modundaydı); hesapla sığıyor.

## D5 (3)-D7 · Teçhizat, yoklamalar, ayarlar, fotoğraf, kalite turu (2026-10-06 öğleden sonra)

- **Yansıma yoklamaları:** 54 küp harita (3×2×9 ızgara, 128², 6 mip), her pürüzlülük fırında ayrı izlenir (GGX; sonradan
  süzme yok). Bir parçanın içinde kalan 5 yoklama geçersiz sayılır. Canlıda üç doğrusal okuma, paralaks düzeltmeli,
  yalnız metal ve parlak yüzeyde. **Bir mip'in 54 yoklaması tek başlatmada:** yoklama başına başlatmada küçük mip'ler
  birkaç yüz iş parçacığıydı, kart boştu (pürüzlü mip'leri 4-8 kat örnekle 64 örnekli fırın 75 → 113 sn; tek başlatmayla
  76 sn). Saten alüminyumdaki lekeler pürüzlü mip'in gürültüsüydü, gitti.
- **Gösterge camı** ayrı katman (Fresnel + parlamalar, pürüzlülük 0,07).
- **Yüzeye göre tessellation hedefi:** kabartmalı yüzeyler (diş, kaynak, mantar, oyma) 1×, boya ve döküm 6×, kabartmasız
  8× kaba. Siyah etiketler (oyma yok) de 8×. 1440p 4x High 51 → 25,5 ms.
- **Ayar merdiveni (D6):** Tessellation, Grafik, Işık, Efekt ayrı ayrı Low/Medium/High; çözünürlük 720p-8K, SSAA
  1x-16x. **Fotoğraf** (F12): 1920×1080 parçalar, 192 px koruma bandı, alt çerçeve izdüşümü; pus için parçanın gerçek
  boyutu (ilk denemede parça sınırlarında pus dikişleri). 8K 16x 16 parça ~1 sn. **Kamera çarpışması:** 5 cm doluluk
  ızgarası, 10 cm yarıçap, eksen eksen kayma. **İlk açılışta fırın ilerlemesi** yükleme ekranında (`Host::loadProgress`).
- **Hata (bulundu, düzeltildi): en ince üçgen tabanı 14,8 mm idi.** Bant sınırının tabanı (`finestTriangle`) en uzun
  taban kenarından türetiliyordu; levhaların yelpaze üçgenlerinin uzun kenarları onu 14,8 mm'ye çekmiş, High'da bütün
  ince kabartma sessizce süzülüyordu. Sabit 0,1 mm (kare süresi değişmedi).
- **Oda kapalı kabuk:** canlıda ışık hesabı yok, her yüzey en az pusu alır: tam siyah piksel = hiçbir şeye çarpmayan
  bakış. `--test oda-catlak` altı açıdan High'ı bölmesiz tabanla karşılaştırır, `--siyah-maske` deliği boyar. Bulunanlar
  modelleme boşluklarıydı: güverte kenarında 2 cm şerit (köşebent eklendi), levha aralıklarından güverte altına bakış
  (pompa dairesi tabanı ve duvarları, perdeler güverte altına iner), perdenin gövde-güverte köşesini kirişle kesen
  açı örneği (köşeye tam köşe noktası). 6874 → 251 → 53 → **0** (6 kamera, 1x ve 4x, High ve bölmesiz).
- **Ateş böceği sınırı 0,5 → 0,06** (`bake.clamp`): dolaylı bir yolun parlaklığı odanın en parlak yüzeyinin ~2,5 katıyla
  sınırlı. 0,5'te Christmas tree'nin yanındaki sıcak noktayı bulan tek yol 64 örnekte bir texel'in bütün ışığıydı: mantar
  yeşil benekliydi (512'de de silik kalıyordu).
- **Phong çarpanı 0,75 → 0,5:** bir kenarın ortası x = r cos t (1 + a sin² t); a = 0,5 çember yayına ikinci dereceden
  oturur, 0,75 yarım sagitta dışarı itiyordu (20 kenarlı disk tırtıklıydı). **Düz yüzü yuvarlakla buluşan parçalarda
  (silindir kapakları, torna profilleri, halkalar, kubbeler) 0,05 mm yay sapması:** düz yüzün kenarındaki normaller
  eksenel, Phong o çokgeni yuvarlayamaz. Borular, kablolar, simitler, küreler eskisi gibi (normalleri hep radyal; hepsine
  uygulamak 7 → 8,1 milyon üçgen ve +5 ms'ydi).
- **Torna profillerinde keskin köşeye 0,6 mm yay:** köşede tek normal Phong'la iki yüzü de şişiriyordu (lamba yakasının
  düz tabanı küreye gömülüyordu). **Etiket ve dış hat normalleri kenar uzunluğuyla ağırlıklı:** yay ile doğrunun
  buluştuğu nokta doğrunun normalini alır; etiketler yastık gibi şişiyordu.
- **Panolar (iç haberleşme, cayro, DC dağıtım, dış pano):** kapaklara bölünmüş ön yüz (4 mm çıkık, 2,5 mm aralık), üç
  menteşe topuzu, çeyrek dönüş kilitler, sayaç paneli (bakalit kutuda amper/volt metreler, etiketleriyle), sıra sıra
  döner şalterler (krom kalkan, OFF/ON çizgileri, bakalit kol, beyaz işaret çizgisi), altında devre adıyla siyah etiket
  (78 devre adı, REFERANS'tan), en alt sırada sigortalar, panjur, tekme levhası. Üst-arka köşe gövdeye uyan pahla
  kesildi (postalar arkadan geçer).
- **Postalar:** geçici bir denetim her istasyonun posta gövdesine ve iç flanşına giren köşelerini saydı. Panolar
  gövdeden 10-12 cm içerdeydi, flanş 12,5 cm: hepsi gövdeye teğet (yüzü eksene) 18-30 cm içeri alındı. Kalan üçü tavandaki
  geçişler (kuyular, kaporta, bölmenin üstü), gerçekte de postayı keser.
- **Kırmızı lambalar** posta 52 ve 53'ün tam üstündeydi (postanın gövdesi küreyi kesiyordu); posta aralarına alındı. Kafes
  gerçek yaylarla (üst halka, dört kol, alt halka); küre ampulün sıcak noktasından kenara koyu kırmızı (düz pembe disk idi).
- **Seyir haritası:** çıkartma atlasında yeni tür, 0,2 mm/piksel (2800×3900). Uydurma Ege kıyısı (gerçek bir kıyının
  yanlış kopyası hata gibi okunurdu): kara, sığlık mavisi, 2-200 m derinlik eğrileri, kıyı, karada yükselti eğrileri, ada
  ve fener (Fl(2)10s), kayalıklar, batık, iskandiller ve dip cinsi, 5'lik enlem-boylam ağı ve dakika çubukları, eflatun
  pusula gülü (sapma 5°E), Türkçe künye, nöbetçinin kurşun kalem rotası (saatli mevkiler, DR yarım dairesi, silinmiş eski
  rota), kahve halkası, kat izi. Kâğıt geometride milimetrelik dalgalı, bir köşesi kalkık. Masada paralel cetvel ve kalem.
- **Fırında örnekler texel'in alanına yayılır:** her örnek texel'in ayak izinde başka bir noktadan çıkar (araya bir şey
  giriyorsa merkezden); texel ayak izinin ortalamasını tutar. Cetvelin temas gölgesi haritanın texel ızgarasında testere
  dişiydi; şimdi basamaksız. **Harita ve masa 1 mm texel.**
- **Telsiz odası bölmesi:** alüminyum yerine boyalı levha (Türk teknesinde her yer krem), vida sıraları, kapıda panjur,
  gözetleme camı, "TELSİZ ODASI" şablonu.
- **Boya yongaları:** kenarın tepesinde gürültünün ~%85'i yonga açıyordu (bütün kenar paslı bir dikiş); şimdi ~dörtte
  bir, çeliğe inen onda birin altında, güverte yanında biraz fazla.
- **Ölçümler (son durum, 512 örnek):**
  - Taban ağı 7,27 milyon üçgen, 11.337 chart; ışık atlası 8192×10383, 62,0 milyon texel (%73 dolu).
  - **İlk açılış 5 dk 15 sn** (512 örnek, pişirme 308 sn; Özcan'ın tavanı 6 dk). Sonraki açılışlar önbellekten.
  - Kare süresi, 1440p, başlangıç görünümü (GPU zaman damgası, 8 kare; Grafik/Işık/Efekt High):

| Tess | 1x | 2x | 4x | 8x | 16x |
|---|---|---|---|---|---|
| Low | 7,9 | 8,7 | 13,4 | 22,9 | 30,9 |
| Medium | 8,7 | 9,4 | 15,0 | 26,2 | 34,6 |
| High | **12,6** | 15,4 | **30,7** | 68,9 | 109,8 |

    High 1x başka çözünürlüklerde (taş ortasında): 1080p 8,6 ms, 4K 20,4 ms. Odada 1440p High 1x 7,5-13,3 ms.
    Özcan'ın canlı bütçesi 1440p High 1x'te 25-33 ms: pay var.
  - Canlı uygulama (`--autotest-only denizalti`, 1440p 4x High): 34 FPS, 28,5 ms, kart belleği 5,4 / 16 GB.
  - Oda çatlak testi: 6 kamera × 1x/4x × High/bölmesiz, **siyah piksel 0**.
  - Kararlılık (48 kare yürüyüş + dönüş, canlı 4x / referans 16x; `tools/kararlilik.py`): başlangıç yolu piksel
    kıpırtısı 0,00073 → **0,00056** (blok 0,00017 → 0,00014), FLIP 0,0044 → **0,0037**; yakın yol (trim görünümü)
    0,00032 → **0,00030**, blok 0,0001, FLIP 0,0020 → **0,0018**. Karşılaştırma: Ada ~0,001.

## D4-D5 · Malzeme, ayrıntı, ışık ve atmosfer (2026-10-06 öğleden sonra)

- **Aşınma (yerinde, deterministik):** kenarlarda boya kırığı (kırmızı astar, en kötü yerde çıplak çelik; yalnız kenar
  ağırlığı olan yerde: yüzde kırık = gren gibi benek, ilk denemede görüldü), oyukta kir (fırının 25 cm AO'su), yukarı
  bakanda toz, pirinçte kararma, güvertede yürüme yolu sürtünmesi. Mantar boyası saten (taneler parıldamasın).
- **Önceden hesaplanmış gürültü hacmi** (128³, gradyanıyla, mip'li): fbm her oktavı tek süzgeçli okuma. Kare 56 → 38,7 ms.
- **Çıkartma atlası** (`dz_decal.*`): kadran yüzleri CPU'da (16" derinlik göstergesi 2048 px = 0,2 mm/px; 0-165 ft,
  "SALT WATER DEPTH TO KEEL"; 0-600 derin; 0-4000 hava; beyaz yüzlü 0-3000/0-300; amper metre; pusula kartı), siyah ve
  oyma pirinç etiket levhaları (pirinçte yazı High'da 0,25 mm oyulur), şablon boya yazılar: **Türkçe** "SU GEÇMEZ KAPI /
  SEYİRDE KAPALI TUTULACAK" ve tatbikat kartı (sanat kararı: Türk hizmetinin eli; kaynakta yok, uydurma olduğunu biliyoruz),
  posta numaraları. Aynada yüzey koordinatları da aynalanır (ilk karede yazılar tersti; diş de sol el olmuştu).
- **Işık dengesi:** kırmızı gece lambaları kısıldı; harita lambası tek beyaz havuz (odak); Christmas tree'nin yeşili üç
  küçük ışıkla (fırında; parlamasız). Ateş böceği sınırı (dolaylı ışık parlaklığı ≤ 0,5 ≈ odanın en parlak yüzeyinin 20
  katı): panele çok yakın küçük ışıklar odaya yeşil benek saçıyordu. SH okuması tek yönle (parlaklığın): zayıf kanalın
  kendi yönü gürültü, tümsekli yüzeyi beneklere boyuyordu.
- **Atlas içinde kenar koruyan süzgeç** (5×5, aynı chart, ışık benzerliği), büyük düz yüzeyler 3 mm teksel; fırın
  sonucu doğrudan yarım duyarlıklı düzlemlere (RAM ~13 → ~6 GB). Atlas 50 milyon teksel; 256 örnek 118 sn.
- **Pus:** fırında 4 cm hücreli hacim (124×66×206), her hücre lambalara gölgeli doğrudan örnekleme + dolaylı (256 örnek,
  birkaç sn). Canlıda 160×90×64 froxel, logaritmik derinlik, Frostbite integrali, tavana doğru yoğunlaşan duman/nem
  katmanı (sanat: 1970'ler, kapalı tekne), ileri saçılım g = 0,35. Yoğunluk ×3,5 (×6 fazla bulanık, ×1 görünmüyordu).
- **Bloom:** 13 dokunuşlu aşağı (ilk seviye Karis ağırlıklı), çadır yukarı, %4,5 karışım (enerji korunur). Kırmızı
  küreler beyaz disk değil kırmızı ışık.
- **Gövde servisleri:** üst ve alt şeritlerde 12 boru hattı (postaların önünden, her postada kelepçe + askı, flanş/rakor,
  hat üstü vanalar), 4 kablo borusu + buat kutuları. **Eşyalar:** CO2 söndürücü, savaş feneri, askıda iki kulaklık,
  gemi saati, tatbikat kartı, kafesli vantilatör.
- Ölçüm: 5,45 milyon taban üçgeni; kare 1440p 4x High 45-47 ms (iyileştirme sırada).

## D3 (1. geçiş) · Oda ve teçhizat (2026-10-06 öğle)

- `dz_room.*`: gerçek ölçülerle kabuk (REFERANS §0): 8,23 m, gövde ekseni güvertenin 12 cm üstünde, sacın içi 2,421 m,
  **1" mantar** (FEAT_CORK: boyanın yuvarlattığı taneler), 11 T posta (6×4, 76,2 cm), iki perde ve **oval su geçmez
  kapılar** (51×97 cm, eşik 48 cm, eksenin 1,1 m sancağı; döküm çerçeve, 8 köpek, çift topuzlu kol, pirinç çerçeveli
  pencere), yeşil boyalı güverte levhaları (gömme vidalı), tavanda 6 kablo demeti (örgü yüzey, kuşaklar), 24 cm
  havalandırma kanalı (flanş, askı), borular; 3 kırmızı gece lambası (buhar geçirmez, kafesli küre), harita lambası
  (emaye abajur, tek beyaz kaynak).
- `dz_equip.*`: dalış istasyonu (eğik pano, iki 16" ve bir 8,5" derinlik göstergesi, eğim göstergesi, dümen açısı
  göstergeleri, 70 cm 8 kollu çıplak çelik çarklar, değiştirme valfi kolları, turkuaz vinil oturaklar), Christmas tree
  (9×7 kırmızı/yeşil mercek; iki açıklık hâlâ kırmızı), 6 kollu ana vent manifoldu, hidrolik yağ tankı, trim manifoldu
  (2×8 kırmızı el çarkı + gösterge panosu), telsiz odası bölmesi (alüminyum, sürgülü kapı), harita masası (çekmeceler,
  harita, pergel, kupa), dümen standı (kırmızı bantlı çark, gyro tekrarlayıcı, dümen açısı göstergesi), kule merdiveni ve
  oval kaporta boğazı, iki periskop kuyusu, HP hava manifoldu (18 gösterge, 3 kat valf), elektrik panoları.
- **Hata (bulundu, düzeltildi): oda aynaydı.** "x sancak, y yukarı, z baş" adlandırması baş tarafa bakan biri için sol
  elli: sağ elli dünyada +z'ye bakınca +x solda kalır. Dalış istasyonu (iskele) karede sağda çıktı. Bütün dünya x'te
  aynalanıp üçgen sırası çevrildi (`mirrorX`); bitmiş dünyada x iskeleye.
- **Ölçüm:** 4,69 milyon taban üçgeni, 2,49 milyon köşe, kurulum 1,1 sn. Atlas 8192×13156, **86,8 milyon teksel**;
  pişirme 256 örnek 144 sn, 1024 örnek ~10 dk. Kare 1440p 4x High: 51,5 ms → **küme ayıklaması** (0,5 m hücreler,
  çerçeve dışı kümeler çizilmez, komşu aralıklar birleşir) + kontrol aşamasında arkası dönük yamalar: **36,0 ms**.

## D1 (1. kısım) · Fırın: yönlü ışık haritası (2026-10-06 öğle)

- `dz_atlas.*`: her ızgara bir chart; chart'ın kendi yüzey koordinatları (metre) atlas dikdörtgenine; rafa dizme;
  küçük parçalar (< 6 cm) iki kat ince. Teksel-yüzey eşlemesi: teksel merkezi üçgenin içinde ya da 0,75 teksel
  yakınında (chart kenarı da aydınlanır), sonra boş kenar teksellerine komşu ortalaması (padding).
- `dz_bake.*` + `device/dz_bake.cu` (OptiX, NVRTC): teksel başına N yol; lambalara doğrudan örnekleme (küre ışık,
  gölge ışını: kafes gölgesi düşer), sekmelerde yeniden doğrudan örnekleme, Rus ruleti; gelen ışık L1 küresel
  harmoniğe (miktar + yön). Ayrıca dört ana lambanın teksel başına görünürlüğü (parlamanın pişmiş gölgesi: 2010'ların
  "sabit ışık" yolu; canlıda yalnız okunur). Kısa başlatmalar (~250 ms, sürücünün 2 sn bekçisinin çok altında).
  Önbellek `cache/denizalti/isik.bin` (git dışı), anahtar: geometri + lambalar + ayarlar.
- Gölgelendirici: yaygın ışık L1'den doğrusal olmayan kurulumla (Geomerics: negatif lob yok), ortam yansıması
  harmoniklerden (yoklamalar gelene kadar), lamba parlaması küre ışık (Karis) × pişmiş görünürlük.
- **Ölçüm:** test duvarı 8192×1953 atlas, 8,96 milyon teksel (%56 dolu); **64 örnek 2,7 sn, 1024 örnek 16,3 sn**.
  Kare 1440p 4x High: 16,6 ms (D0'ın geçici ışığıyla 13,5 ms).
- **Bulunan hata:** ilk pişirmede duvarda tek tük parlak teksel. Sebep: yarım küreyi kosinüsle örnekleyip RADYANSI
  harmoniğe izdüşürürken ağırlık π/cos sıyırma açısında patlıyor. Düzgün yarım küre örneklemesi (ağırlık 2π, sınırlı):
  benekler gitti. Kalan: 1024 örnekte ince lekelenme → son pişirme daha çok örnekle.
- Hata: `dilate` 16 kanallık geçici diziyle 20 kanalı yazıyordu → yığın taşması (0xC0000409, çıktısız). Düzeltildi.

## D0 · Hat ve test duvarı (2026-10-06 öğle)

**Yapılan:**
- Sahne sözleşmesine `texture()` eklendi: sahne kendi çizdiği GL dokusunu verir (`src/app/scene.h`, `app.cpp`).
  Öteki sahneler değişmedi (`image()` yolu aynen).
- `dz_gl.*`: OpenGL 4.6 yükleyici (Khronos `glext.h`, `third_party/fetch.sh` indirir).
- `dz_geom.*`: parça kiti. Her kenar yuvarlatılmış; ızgara yamaları; tüp, dirsek, halka, yuvarlak silindir, altıgen,
  yuvarlak kutu, simit, kubbe, küre, gövde duvarı, torna (lathe), ekstrüzyon. `finishMesh`: aynı konumdaki köşelerin
  normalleri ortalanır (yer değiştirme normali, çatlak olmasın), teğetler.
- `dz_parts.*`: boru hattı (dirsekli), flanş çifti + saplama ve somunlar, el çarkı, glob vana, gösterge, gövde duvarı,
  T kesitli posta + kaynak dikişi, tread güverte levhası.
- `dz_world.*`: test duvarı (iskele tarafı gövde, 4 posta, 5 vanalı manifold, flanşlar, gösterge panosu, 3" boru).
- `dz_render.*` + `glsl/`: tessellation (kenar çarpanı, kenarın uzunluğu bölü hedef boyun o uzaklıktaki karşılığı;
  yalnız kenarın iki ucundan hesaplanır), Phong tessellation (paylaşılan yer değiştirme normaliyle), analitik yüzey
  ayrıntısı (sac dalgası + portakal kabuğu boya, kaynak dalgası, diş, tread, döküm) hem yer değiştirmede hem normalde
  aynı işlevle; çok örnekli hedefte örnek başına gölgelendirme (gerçek SSAA); ton sıkıştırmalı çözümleme; AgX + sabit
  titreşim; pencereye Catmull-Rom/çadır süzgeçli uydurma. Ters derinlik.
- Canlı sahne: galeride 08, panelde Çözünürlük / SSAA / Tessellation / Grafik / Işık / Efekt; WASD, Tab bakış, sol
  sürükle bakış, sağ tık dürbün (görüş 4 kat dar), Shift yavaş, R baş.
- Işık D0'da **geçici** (üç lamba, gölgesiz): yalnız hattı ölçmek için. Özcan'a bu kareler gönderilmedi.

**Ölçümler (RTX 4080, sürücü 617.14):**
- GL 4.6; çoklu örnek renk ve derinlikte **32'ye kadar**: 16x SSAA donanımın kendi 16 örneğiyle (2×2 ölçek gerekmedi).
  Tessellation çarpanı en çok 64.
- Test duvarı: 601 bin taban üçgeni, 324 bin köşe, kurulum 104 ms. En ince üçgen 2,2 mm/64... (taban kenarı 12 mm'den).
- **Kare süresi, 1440p** (GPU zaman damgası, 8 karenin ortancası), ilk sürüm → analitik türev sonrası:

| Tess | 1x | 2x | 4x | 8x | 16x |
|---|---|---|---|---|---|
| Low | 2,1 → **1,0** | 2,4 → 1,2 | 5,5 → **2,2** | 11,0 → 4,9 | 20,2 → **8,2** |
| Medium | 3,2 → 1,7 | 3,4 → 1,9 | 7,6 → **3,4** | 16,3 → 7,7 | 30,5 → **13,3** |
| High | 7,9 → 4,9 | 10,3 → 7,0 | 24,0 → **13,5** | 53,7 → 30,4 | 109,4 → **57,0** |

  Maliyet örnek sayısıyla doğrusal: örnek başına gölgelendirme baskın. İlk sürümde normal ayrıntısı merkezi farkla beş
  kez gürültü hesaplıyordu; analitik türevle bir kez → yarıya indi. Çözümleme 16x'te 0,3 ms.
- **Çatlak testi** (`--test catlak`, arka yüzler macenta, ayıklama kapalı): 4 kamera × 3 seviye × 1x/4x. Bulgu: milyon
  pikselde birkaç macenta. Bölme kapalıyken (`--bolmesiz`) kayboluyor, yer değiştirmeden bağımsız. Yakından bakınca:
  biri borunun **siluetinde** (ayıklama kapalı görünümde ön ve arka yüzün eşit derinlik çakışması), öteki somun
  kapağının ortasında tek pikseller. Köşeleri konuma göre sabit sırayla toplamak (TES) değiştirmedi. Normal çizimde
  (arka yüz ayıklı) görünür bir delik gözlenmedi, ama bu **ölçülmedi**: test, oda kapalı kabuk olunca normal çizimle
  yeniden kurulacak (D3).
