# DURUM · sahne 4 "Tanecik"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/tanecik/PLAN.md`'de.

## Şu an
- **2026-10-02 (gece) · T0 (görünüm) bitti, Özcan'ın sınamasını bekliyor.** Kareler `docs/tanecik/kareler/t0_*.jpg`.
  Paralelde (Özcan'ın izniyle iki ajan, ayrı çalışma kopyalarında): T1 tanecik çözücüsü çekirdeği
  (`C:\dev\pc_graphics_wt\cekirdek`, dal `tanecik-cekirdek`) ve sıvı yüzeyi (`C:\dev\pc_graphics_wt\yuzey`, dal
  `tanecik-yuzey`); bitince `main`'e birleştirilecek.
- **2026-10-02 · Plan onaylandı, T0 başladı.** Özcan: *"Onaylıyorum. Başlayabilirsin."*

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
  parçanın kemiğine ait. **172.555 tanecik**, 59,2 L (yarım aralıklık deri payı hariç), deri katmanı %19. Kemik başına:
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
  - **Renk kararı (tek renk + tek vurgu):** duran bedenin tanecikleri soluk pembe (0,80 0,55 0,50), kemik fildişi;
    sahnedeki tek doygun renk hasarın serbest bıraktığı koyu kırmızı sıvı olacak (0,35 0,02 0,02). Kırmızı
    taneciklerle dolu cam adam ilk denemede "kanla dolu" okunuyordu; Özcan gore istemiyor.
  - **Tanecik görünümü:** hepsi (çizim yarıçapı aralığın 0,2'si: ince bir gren), seyrek (%10, 0,42) ya da gizli. Yalnız
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
