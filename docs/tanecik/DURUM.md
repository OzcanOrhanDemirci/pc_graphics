> 🗄️ **ARŞİV (2026-10-03).** Sahne 4 "Laboratuvar"a döndü (sıvı ve tanecik fiziği, ağır çekim); insan yolu fikir
> olarak kaldı. Kod `tanecik-insan-arsiv` etiketinde. Güncel sahne: `docs/laboratuvar/`.

# DURUM · sahne 4 "Tanecik"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/tanecik/PLAN.md`'de.

## Şu an
- **2026-10-02 (gece) · T0 (görünüm) bitti, Özcan'ın sınamasını bekliyor.** Kareler `docs/tanecik/kareler/t0_*.jpg`.
  Paralelde (Özcan'ın izniyle iki ajan, ayrı çalışma kopyalarında): T1 tanecik çözücüsü çekirdeği
  (`C:\dev\pc_graphics_wt\cekirdek`, dal `tanecik-cekirdek`) ve sıvı yüzeyi (`C:\dev\pc_graphics_wt\yuzey`, dal
  `tanecik-yuzey`); bitince `main`'e birleştirilecek.
- **2026-10-02 · Plan onaylandı, T0 başladı.** Özcan: *"Onaylıyorum. Başlayabilirsin."*

## Kanama ve canlı silahlar · sürüyor (2026-10-03 gece)
Özcan (ilk atış klipleri): *"Etki olarak iyi duruyor da parçacıklar akmıyor yani kan akmıyor."* Doğru: yalnız yol üstündeki
birkaç düzine tanecik kana dönüp 60 m/s ile saçılıyordu.
- **Kanama** (çözücüde `addBleed`, `bleedFade`): giriş ve çıkış deliğinin çevresindeki (3 cm) ve kanal boyundaki bağlar kendi
  hızıyla kopar (karma ile birebir), pıhtılaşmayla söner. 9 mm'de kan 37 → 5 sn'de 679 tanecik (~230 mL). **Yara basıncı**
  (`woundPressure`): delik çevresindeki sıvı dışa itilir. **Islatma** (`fizik.islatma`, `fizik.islak_surtunme`; varsayılan
  kapalı): sıvı katıya yapışır ve onunla sürüklenir.
- **Sonuç: kan hâlâ akmıyor.** Sıvıya dönen taneciklerin çoğu yaranın cebinde, yırtık dokunun arkasında kalıyor; dışarı
  çıkanlar basınçla (0,4 m/s) deriden kopup serbest düşüyor; ıslatma 20'de birkaç damla deriye yapışıyor ama akmıyor.
  Sıradaki: kanın dışarı yolu (ceple deliği bağlayan akış; basınç deriye teğet), deride ince akıntı (ıslatma + sürtünme
  ayarı), çok damla birleşip akıntı olsun diye yeterli hacim. Klipler `captures/tanecik/beden_kanama*.mp4` (masaüstüne
  kopyalanmadı).
- **Canlı pencere** (`BodyWorld`, `tanecik_live.cpp`): tabanca / tüfek / pompalı (1, 2, 3), gerçekçi sınırlı güç çubuğu
  (yükün adı, kütle, hız, momentum, 70 kg'a en çok hız), sol tıkla kameradan imlecin altına atış, T ağır çekim, P zamanı
  durdur (kamera döner), R yeni manken. Ayaktayken iskelette zemin teması kapalı (kare 103 → 35 ms, 27 fps).
  Bulunan: 4 m'deki kameradan her atış ıska geçiyordu (mermi yolunun ortam haritası namludan 3 m'ydi); artık bedenin hemen
  önünden başlar. **Doğrulanmadı:** düzeltmeden sonraki otomatik testte tıklamalar arka duvara düştü (sahne daha hızlı
  yüklendiği için betiğin adımları kaymış olabilir); canlı pencerede bir isabet henüz görülmedi.
- Ajanlar: süzgeç ve denge sürüyor; Özcan: bunlar bitince yeni ajan yok (kota).

## T2 + T4 ilk hâl · beden çözücüde, mermiler bedende (2026-10-03 gece)
Özcan (T1 klipleri): *"Valla çok gerçekçi duruyor, devam et bakalım. Özellikle mankendeki etkileri merak ediyorum, kan da
çok iyi duruyor"*.
- **BodySim** (`body_sim.*`): kemik tanecikleri **kinematik** (iskeletin kemiklerini izler; çözücüye kinematik sürüş, hız
  ekleme, bir taneciğin bütün bağlarını koparma eklendi), her kemiğin eti ayrı sıvılaşan grup (kolun eti göğsünkünden geçmez),
  bağlar çizgeden (doku 1e-3, deri zarı 2e-4, kemiğe 3e-4 m/N; kopma 0,5 / 0,6 / 0,5). Dinlenmede temas mesafesinde olup
  kurallarca bağlanmamış 676 çifte **gevşek bağ** (zayıf, çabuk kopar). Manken şimdilik **görünmez ayakta** (leğen tutulur,
  kaslar duruşu tutar); denge ajanda. Ayakta 3 sn: kopuk bağ 26, son 0,5 sn'de en çok 2,8 mm kayma (ortalama 0,5 mm).
  - Titreşim: 2 bağ yinelemesinde ~100 tanecik 0,4 m/s'ye kadar titriyor (milimetre altı, görünmez); 4 yinelemede 18
    taneciğe iner. **XPBD bağ sönümü** (Macklin 2016) denendi: en küçük değerde bile beden dağıldı (19 bin bağ koptu);
    kodu duruyor, kapalı (`fizik.bag_sonum`), ayrıca incelenecek.
- **Mermi bedende** (`body_hits.*`): ışın boyunca ortamlar taneciklerden (kemik / doku / hava), mermi `weapons::traceShot`
  ile; yol üstündekiler momentumu alır, bütün bağlarını yitirir (kana döner); geçici boşluk çevreyi dışa iter (simetrik,
  net momentum yok); aynı momentum kemiğe de itki. 2 mm adımların taneciksiz kalanları sonraki taneciğe devredilir.
  Deri, tanecikleri kana dönen ya da 2,5 aralıktan fazla açılan köşelerde yırtılır (önce fırlayan tanecik deriyi iğne
  gibi uzatıyordu).
- Klipler (göğse, 2,9 m'den; atıştan sonra 0,6 sn ×0,1 ağır çekim), masaüstünde `Tanecik T4 ilk atislar`:

| Atış | Yol üstünde | Boşlukla itilen | Kopan bağ | Momentum (verilen / bırakılan) |
|---|---|---|---|---|
| 9 mm | 36 | 2.927 | 450 | 1,58 / 1,59 N s, 20 cm dokudan çıktı |
| 7,62×51 | 27 | 4.282 | 349 | 1,34 / 1,42 |
| .50 BMG | 70 | 5.941 | 775 | 3,05 / 3,05 |
| 12 kalibre 00 (9 saçma) | 269 | 21.998 | 3.097 | 9,74 / 10,06 |

  Açık: pompalının yarası büyük (~15 cm; boşluğun ittiği doku bağlarını koparıp kalıcı yaraya dönüyor: boşluk payı ve
  doku kopma gerilimi ayarlanacak); kan saçıntısı tek tanecik damlaları (ince sis yok); manken kemiğiyle neredeyse
  kıpırdamıyor (gerçekçi: .50 BMG 3 N s, ama görünmez ayak da tutuyor).

## T1 · tanecik çözücüsü (2026-10-02 gece, ajan dalı `tanecik-cekirdek`, main'de)
`src/scenes/tanecik/particles.*`, testleri `tanecik_tests.cpp` (`--test [ad]`, `--clip baraj|balon`). FleX yolu: PBF sıvı
(tek yönlü, çekirdek 1,8 aralık, alt adım başına bir diverjans geçişi: duran tank "kaynıyordu", 40 bin tanecik > 0,1 m/s
→ 0), duvarlar yoğunlukta dolu sayılır, katılar sıvının yoğunluğuna Akinci ψ ile girer (ince deri suyu tutar), Akinci
yapışması 5 m/s² (kapalıyken birikintinin %47'si yalnız tanecik), XPBD bağları renkli Gauss-Seidel (bağ yinelemesi 2;
1'de zar kendiliğinden patlıyordu), şekil eşlemeli katı gruplar (küp; T2'de kemik), sıvılaşma, toplama ile birebir
tekrar, CUDA grafiği. Sıvı-zemin sürtünmesi 0,3 (0'da tek tanecik kalınlığında film sonsuza kaydı).

| Test | Sonuç |
|---|---|
| Serbest düşüş | 0,4503 / 0,4515 s (%0,27) |
| Hacim (tank) | ortalama yoğunluk sapması %0,79, yükseklik +%0,44 |
| Baraj (Martin ve Moyce 1952) | cephe RMS 0,069 (0,09 zaman kaymasıyla; kaymasız 0,118), ölçüt < 0,15 |
| Balon | dururken %0,05 su kaybı, 0 kopuk bağ; yandan vurulunca 1 sn'de %28, 3 sn'de %62 boşalır; hafif küp (0,17 kg) patlatmaz, ağır küp (13 kg) %95 boşaltır |
| Birikinti, dökme, sıvılaşma, tekrar | geçti (tekrar: 17.669 tanecikte 0 fark) |
| Bütçe | beden 173 bin tanecik, 2,05 milyon bağ: 20 alt adımda 18-19 ms (bağlar 8,4, komşu arama 4,1); 195 bin su 24 ms |

Kırmızı denetim: yoğunluk kapalı → hacim kalır (−%96,6), bağlar kapalı → balon kalır, yapışma kapalı → birikinti kalır.
Açık: ince saçılmada tek tanecik kalınlığında "erişte" iplikleri; ağır küp patlaması kaotik (aynı derlemede birebir,
ayarla %11-95 arası); zar sertliği bağ yinelemesine bağlı (yakınsama sınırlı).
Klipler (sıvı artık yüzeyiyle ve berrak su malzemesiyle): `captures/tanecik_t1_balon.mp4`, `captures/tanecik_t1_baraj.mp4`;
masaüstünde `Tanecik T1`.

## T2 hazırlığı · ajanlar çalışırken (2026-10-02 gece)
Fiziğe dokunmadan, çözücü gelince bağlanacak parçalar (Özcan: *"Sen agentler çalışırken başka bir işe devam et"*):

- **İskelet tek kaynakta** (`skeleton()`): kemiğe etiketli parçalar (kapsül, elipsoid, kaburga tüpü). Kemiklerin ağı da,
  kemik sayılacak tanecikler de buradan (önce iki ayrı yerde yazılıydı).
- **Bedenin fizik çizgesi** (`body_graph.*`, düz veri): doku / deri / kemik tanecikleri (kemik parçasının içindeki
  tanecik o kemiğe geçer; ince kemik en az 0,75 aralık yarıçapla aranır), komşu bağları 1,5 aralıkta: doku (aynı ya da
  eklemle komşu kemik; iki uyluk birbirine bağlanmaz), deri zarı, kemiğe tutunma (yalnız kendi kemiğine). 7 mm'de
  **1,04 milyon bağ** (doku 874 bin, deri zarı 102 bin, kemiğe 63 bin), tanecik başına 12; bağsız tanecik 0. Kemik
  kümeleri 129 (el) ile 5.957 (kafatası) tanecik. **16 eklem:** omurga ve boyun, omuz, kalça, bilek, ayak bileği top
  eklem (koni + burulma sınırı); dirsek ve diz menteşe (dirsek eli öne, diz ayağı arkaya büker). `--stats` yazdırır.
- **İleri kinematik** (`forwardKinematics`) ve **fiziksiz duruş sınaması** `--still --poz dirsek|kol|diz|egil|karma`:
  kemikler, tanecikler ve deri birlikte izliyor (kareler `captures/tanecik/v/montaj_poz.png`). Omuzda küçük bir yırtık:
  kemik dönerken esneyecek doku yok (fizik gelince kapanır).
- **Çizici hareketli veriye hazır:** kemik başına bir örnek ve dönüşümü; deri her köşesiyle dört deri taneciğine bağlı,
  pay kemiklerinin dönüşüyle çevrilir (`tanecik_kernels.cu`, deri yapısı her karede hızlı yeniden kurulur); çizilen
  tanecikler ekran kartındaki konumlardan toplanır; sıvıya dönen tanecik küre olarak çizilmez.
- **Sıvı yüzeyi birleştirildi** (ajan dalı `tanecik-yuzey`, testleri main'de 8/8) ve çizicide: yüzey ağı + kendi
  yarıçaplı damlalar; **kan malzemesi** Fresnel'li ıslak ayna ve koyu opak gövde (0,13 0,006 0,006).
  `--kan-deneme`: elle yerleştirilmiş birikinti ve damla ipi (fizik yok).
- **Zemin tuval:** 6 m'lik leke haritası (3072², ~2 mm), oturan sıvı buraya işlenir (litre/m², toplam korunur), kurumuş
  kan rengi (0,16 0,028 0,022). Hangi taneciğin oturduğuna fizik karar verecek.
- **Fareyle seçme** (`__raygen__pick`): tıklanan şeyin türü, kemiği / taneciği (çözücünün sırasıyla), konumu ve
  normali. Canlı pencerede sol tık şimdilik ne olduğunu bildirir; araçlar T4'te bununla nişan alacak.
- **Berrak sıvı** (su, bal; T5 için): ışığı kırar (tam Fresnel, iç yansıma), içinde geçtiği yol boyunca soğurur; gölge
  ışınları içinden düz geçer. `sivi.berrak`, `sivi.kirilma`, `sivi.sogurma`. Kan opak kalır.
- **İskelet fiziği** (`skeleton_sim.*`, T2'nin çekirdeği): 17 kemik katı cisim (kütle ve eylemsizlik taneciklerinden),
  16 eklem XPBD (Müller vd. 2020): pivot, menteşe ekseni, menteşe aralığı, top eklemde koni ve burulma; jiroskopik
  terim; eklem sürtünmesi. Ekran kartında, alt adım başına tek iş parçacığı (16 eklem, sabit sıra: birebir tekrar).
  Testler (`--skeleton-test`, şimdilik et kemikle katı: bütün uzuv kütleleri): **asılı** (göğsünden asılı, kollar
  kalkık bırakılır; 5 sn) eklem açıklığı en çok 0,24 mm, sınır aşımı 0,14°, en hızlı 5,0 m/s, enerji 624 → 599 J;
  **serbest** (yerçekimsiz, 9 rad/s döndürülür; sürtünme yok) açıklık 0,33 mm, aşım 0,80°, açısal momentum %3,7 kaydı.
  Klip `--skeleton-clip asili`.
  - Bulunanlar: (1) ilk test 12-20 m/s sıçradı: sebep test duruşuydu (kol 100°, omuz konisi 95°): çözücü 5°'lik aşımı
    bir alt adımda düzeltip ele hız olarak verdi. Duruş sınırın içine alındı; omuz konisi 120° (kol başın üstüne
    kalkabilir). Ders: **başlangıç durumunu kısıtlara karşı ölç** (açıklık ve aşım "başlangıç" satırında). (2) Yalnız kemik
    taneciklerinin kütlesi (el 84 g) kamçı etkisini abartıyor; et kendi fiziğine kavuşunca kemik + bağlı et birlikte
    taşıyacak. (3) Jiroskopik terim olmadan açısal momentum %6,7 kayıyordu.
- **Silah ve patlama modülü** (ajan dalı `tanecik-silah`, main'de; `weapons.*`, `--weapons-test`, 6/6): kaynaklı yükler
  (.22 LR → 9 mm → .50 AE; 5,56 → 7,62×51 → .50 BMG; .410 → 12 kalibre 00 → 3½" magnum; en güçlülerin 70 kg'a verebileceği
  en çok hız 0,125 / 0,54 / 0,30 m/s), dokuda mermi yolu (sürüklenme + dayanım, %10 jele kalibre: 9 mm E15 238 J / 233,
  tepe 2131 N / 2100), geçici boşluk, kemik, Kingery-Bulmash (el bombası 0,20 kg TNT; insana 1 m'de 1,8 m/s), kılıç
  (22 m/s üst sınır), küp 10 kg → 5 t. Tablo farkları: .410 18,2 g 259 m/s; .22 LR 376 m/s; magnum 335 m/s.
- **Paralel ajan:** canlı pencerenin gürültü süzgeci (sürüyor, dal `tanecik-suzgec`).
- **Denge (T3, sürüyor):** iskelete zemin teması (en küçük kareler: düz ayak dönmeden kalkar; nokta düzeltmelerini
  ortalamak ayağı her adımda döndürüp adamı topuktan geri yuvarlıyordu), kas motorları (XPBD esnekliği + güç sınırı),
  `BalanceController` (ayak bileği stratejisi). **Henüz ayakta durmuyor**: denetleyici kapalıyken (heykel) de düşüyor;
  sıradaki iş bunun sebebi. Testler `--balance-test ayakta|merdiven`.

## T0 · görünüm (2026-10-02)
Açmak: galeride 04, ya da `build\pc_graphics.exe --scene tanecik`. Kareler komut satırından:
`--scene tanecik --still --look cam|deri --particles hepsi|seyrek|yok [--yaw --pitch --dist --target --fov --spp]`;
bedenin sayıları `--scene tanecik --stats`.

- **Manken** (`src/scenes/tanecik/body.*`): işaretli uzaklık alanı, elipsoidler ve yuvarlak koniler yumuşak
  birleşimle (gövde geniş, omuz başları gövdeye yuvarlanarak, uzuvlar dar). 1,767 m, A duruşu, yüzsüz manken başı
  (yalnız çene: nereye baktığı belli olsun). **Eklemler tek tabloda** (`restJoints()`): deri parçaları da iskelet de
  oradan kurulur, T2'de fiziğin kemikleri de oradan kurulacak (önce sayılar iki yerde yazılıydı; bacak duruşunu
  genişletince iskelet geride kaldı).
- **Tanecikler:** 7 mm kübik kafes, bir tanecik küresinin sığdığı her yerde (uzaklık ≤ −aralık/2); her biri en yakın
  parçanın kemiğine ait. **173.652 tanecik**, 59,6 L (yarım aralıklık deri payı hariç), deri katmanı %18. Kemik başına:
  göğüs 46 bin, leğen 22 bin, bel 20 bin, uyluk 17 bin, kafa 9,5 bin, el 781. Kafes %25 kaydırılır (hücre başına
  sabit karma: her seferinde aynı beden); düzenli kafes camın ardında kareli kâğıt gibi okunuyordu (%12 yetmedi).
- **Deri:** uzaklık alanından yüzey ağları (4 mm hücre), köşeler yüzeye çekilir, normaller alanın eğiminden; 148 bin
  köşe, 296 bin üçgen. **İskelet:** kafatası, çene, omurga, kaburgalar (öne eğik açık halkalar, paralel taşınan
  çerçeveyle tüp), göğüs kemiği, köprücük kemikleri, leğen, kol ve bacak kemikleri, el ve ayak; 13,7 bin köşe.
- **Çizim** (`device/tanecik.cu`): küçük bir yol izleyici. Beyaz sonsuz fon (zemin yumuşak kavisle duvara), dört
  ışık kutusu (önden-soldan ana ışık, tepe, arkada iki şerit kontur için), açık stüdyonun ortam ışığı, iki dağınık
  sekme. Işık kutuları kameraya görünmez, yansımalarda görünür (kadrajda beyaz şerit olarak çıkıyorlardı). Tanecikler
  OptiX'in yerleşik küreleri.
  - **Cam:** ince, ten renkli bir kabuk. Her geçişte Fresnel yansıması (F0 0,04), %10'u deri gibi saçılır (biçimi
    verir), kalanı geçer ve kabuğun optik derinliği / |cos| kadar renk alır: kontur derinleşir. Optik derinlik
    (0,10 0,22 0,30); ilk denemedeki (0,22 0,48 0,65) içini kahverengi bir plastiğe çeviriyordu.
  - **Renk: kanla dolu.** Duran bedenin tanecikleri koyu kan kırmızısı (0,50 0,06 0,05), kemik fildişi. Önce soluk
    pembeye çevirmiştim (gore kaygısıyla); Özcan düzeltti: *"Gore derken ben onu çok gerçekçi beden yaparsak diye dedim.
    Kanla dolu olması güzel olur. [...] Gerçekte de kanla dolar çünkü. [...] Burada adam manken zaten."* Çizim yarıçapı
    aralığın 0,3'ü: kanla dolu okunur, kaburgalar seçilir (0,2 uzaktan solgun, 0,38 içini tümüyle kapatıyor).
  - **Tanecik görünümü:** hepsi (çizim yarıçapı aralığın 0,3'ü), seyrek (%10, 0,42) ya da gizli. Yalnız
    görüntü, fizik hepsini hesaplar.
  - **Deri:** opak, mat ten rengi (0,55 0,36 0,27) ve hafif parlaklık.
- **Canlı pencere:** kamera mankenin etrafında döner (sağ tık + fare, tekerlek, E/Q); V cam/deri, N tanecikler, K
  kemikler; panelde iki seçim. 1080p, piksel başına 2 örnek, kamera durunca birikir: **162 fps, 3-6 ms** (otomatik test,
  2560×1440 tam ekran).
- **Uygulamaya dokunanlar:** galeri başlığı sahne sayısını kayıttan yazar, liste satırı yüksekliği sahne sayısına göre
  (dört sahne 2560×1440'ta sığıyor; pencere kipinin 0,7 en küçük ölçeği 1280×720'de üç sahnede bile taşıyordu, o eski),
  `--autotest-only <id>`.

**Doğrulanmadı / açık:** hareket hâlinde ince tanecik greninin titreşmesi (fizik yok, yalnız kamera döndü); gürültü
giderici yok (kamera dönerken 2 örneklik gren); yüz ve deri gerçekçiliği bilerek yok.
