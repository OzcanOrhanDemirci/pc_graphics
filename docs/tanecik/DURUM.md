# DURUM · sahne 4 "Tanecik"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/tanecik/PLAN.md`'de.

## Şu an
- **2026-10-02 (gece) · T0 (görünüm) bitti, Özcan'ın sınamasını bekliyor.** Kareler `docs/tanecik/kareler/t0_*.jpg`.
  Paralelde (Özcan'ın izniyle iki ajan, ayrı çalışma kopyalarında): T1 tanecik çözücüsü çekirdeği
  (`C:\dev\pc_graphics_wt\cekirdek`, dal `tanecik-cekirdek`) ve sıvı yüzeyi (`C:\dev\pc_graphics_wt\yuzey`, dal
  `tanecik-yuzey`); bitince `main`'e birleştirilecek.
- **2026-10-02 · Plan onaylandı, T0 başladı.** Özcan: *"Onaylıyorum. Başlayabilirsin."*

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
- **Paralel ajanlar:** çözücü çekirdeği (sürüyor), silah ve patlama fiziği modülü (sürüyor, dal `tanecik-silah`).

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
