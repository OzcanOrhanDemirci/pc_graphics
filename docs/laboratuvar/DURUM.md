# DURUM · sahne 4 "Laboratuvar"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/laboratuvar/PLAN.md`'de. Eski "Tanecik" (insan) yolu
> `docs/tanecik/` ve git etiketi `tanecik-insan-arsiv`'de (arşiv).

## Şu an · 2026-10-03 gecesi (Özcan uyurken, otonom)

**Altı deney çalışıyor, uygulamada galeride 04.** Açmak: `build\pc_graphics.exe` → 04 Laboratuvar, ya da
`build\pc_graphics.exe --scene laboratuvar`. Tuşlar `GOSTERI.md` / `DEMO.md`, ekranda F1. Kareler masaüstünde
`Laboratuvar` klasöründe.

| Deney | Kip | Ne oluyor | Durum |
|---|---|---|---|
| Su | kayıt | beher eğilip bardağa döküyor; yükseklik 12-45 cm; sıçrar, taşar, masada birikir | ince kayıt 1,5 mm, 98.670 su taneciği, saniyenin 1/600'ü; akış düzgün cam bir şerit |
| Bal | canlı | kaşıktan cam tabağa; fareyle gezdir, akış ve yükseklik çubuğu | kesintisiz kehribar ip, tabakta kıvrılarak yığılır, yığın birkaç saniyede göle döner |
| Kum | canlı | huniden masaya ve behere; fareyle gezdir; çelik bilye | yığılma açısı **32,5°** (ölçüt 30-35°); tane dokusu |
| Su balonu | kayıt | masadaki balona iğne (tıkla ya da Boşluk) | 10 ms'de delik, 20-30 ms'de lastik soyulurken su balon biçiminde, sonra çöker |
| Karpuz | kayıt | tabanca ya da tüfekle (5,56 → 7,62×51 → .50 BMG, gerçek yükler) | ~600 J birikir; birkaç ms şişer, kabuk taç gibi açılır; parça görüntüsü yumuşak |
| Jel | kayıt | 24×10×10 cm balistik jel bloğu, tabancayla (.22 → 9 mm → .50 AE) | 9 mm: 24,5 cm'de 329 J; yol boyunca boşluk tüpü, blok sarsılır ve biraz zıplar |

**Kayıt düzeni (karar 4, uygulandı):** tetik kaba dünyada hemen oynar ("Kaba önizleme" yazar); aynı girdilerle ince
dünya arka planda (işçi iş parçacığı, kendi CUDA akışı) hesaplanır, her an ekran kartında saklanır (16 bit konum +
çizim kodu, tanecik başına 8 bayt). Zaman çubuğu hesaplanan kısmı gösterir, tıklanınca hemen izlenir. Bitince bir kez
gerçek hızda oynar, sonra serbest: zaman çubuğu, ×1 / ×1/10 / ×1/100, P dondur, ← → adım, WASD + sağ fare kamera.
Oynatma yalnız çizimdir (fizik yok), anlar arası doğrusal ara doldurma. Video yok.

## Ölçümler

- **Su (ince):** 3 sn dünya zamanı 5,5-7 sn'de hesaplanıyor (kare başına 3,3 ms; uygulamada görüntüyle paylaşınca
  7,8 ms). Kaba canlı 40.829 tanecik (cam duvarlar 19.857 + su 20.972), fizik 7-16 ms.
- **Kum:** `--lab-test kum`: 6 sn dökülen 10.439 tane (2,5 mm), yığın 25,3 mm, taban yarıçapı 39,7 mm → **32,5°**. Sırayla:
  özdeş Jacobi payıyla sürtünme ~0° (sıvı gibi yayıldı) → sürtünme kök payla 14,5° → 4 yinelemeyle 19° → hız
  düzeyinde durgunluk (1,2 cm/sn altındaki tane durur) 32,5°. Eşik 2 cm/sn'de 38,9°, 5 cm/sn'de 59,7° (ayar ölçümle).
- **Balon:** `--lab-test balon`: masada 2 sn, 31.256 bağdan 2'si koptu, zar dışında su 0 (PASS); iğneden 5 ms sonra
  bağların %45'i koptu (PASS, ölçüt 0,15 sn'de > %25). İnce kayıt 2 mm: 119.991 tanecik, 1 sn dünya zamanı ~6 sn.
- **Karpuz (ince 2,5 mm):** 146.953 tanecik (kabuk 45.999, et 98.367); 7,62×51 M80 838 m/s: dokuda 17,7 cm, 595 J,
  0,75 N s bıraktı, mermi çıktı. 5,56 M855: 665 J (sapıp parçalandığı için daha çok).
- **Uygulama (otomatik test, 2560×1440 tam ekran, 1080p iç çizim, 4 örnek + yapay zekâ gürültü giderici):** su canlı
  40 fps, önizleme + ince hesap birlikte 25 fps, bal 33, kum 31, balon kaydı oynarken 11-22, karpuz 20-53 fps.
  Kapanışta boş ekran kartı belleği 14.857 / 16.376 MB (her turda aynı).

## Kararlar ve gerekçeler (bu gece)

- **Çözücü kıyası (karar 6) yapılmadı:** MPM yazılmadı; bütün deneyler T1'in testli PBF/XPBD çözücüsüyle kuruldu.
  Gerekçe: 8 saatte iki çözücü + beş deney sığmazdı; her malzeme önce eldekiyle kareye bakılarak denendi, kum ve
  balon gibi zorlar ölçümle çözüldü. Bal ipi PBF'te tutmadı: MPM'in en çok fark yaratacağı yer orası (aşağıda).
- **Balon asılı değil, masada:** asılıyken hafif lastik suyun ağırlığını düğüme birkaç alt adımda iletemedi, ilk anda
  düğümden koptu (karelerde görüldü). Masada T1 kurulumu gibi masa taşıyor.
- **Zarın bağ yinelemesi 12 (ince 20):** 2 yinelemede 3 mm zar yakınsamadı (etkin sertlik düştü, uzayıp yırtıldı); T1
  balonu 7 mm'de geçiyordu, 3 mm'de aynı ayarla geçmiyordu (ölçüldü: %62,7 su dışarı). XPBD'nin sertliği çözünürlüğe
  ve yinelemeye bağlı.
- **Lastik ön gerilimi oturmada yavaş verilir:** bir anda verilince hafif lastik içe fırladı, su %72 sıkıştı, 4.133 bağ
  koptu. Dinlenme boyu 1 → 0,85 oturmanın ilk yarısında (§15 "yük yavaş verilir").
- **Çatlak ucu kuralı (yalnız iğneden sonra):** bağı kopmuş taneciğin komşu bağları kopma şekil değiştirmesinin 0,4'ünde
  kopar. Kaba kafes çatlak ucunda yoğunlaşan gerilimi çözemez; kuralsız delik %4'te durdu (sızdırdı), 0,6'da %15.
  Dinlenmede kapalı: çözücü gürültüsünün kopardığı 2 bağ gerçek balonda olmayan kusur, kural açıkken balonu
  kendiliğinden patlatıyordu.
- **Su dökmede biraz viskozite + yapışma + yüzey gerilimi** (XSPH 0,05, Akinci 20, gerilim 3): olmadan akış behere
  yakın yerde bile erişte gibi kopuyordu; dört varyant kareyle kıyaslandı (`02 su dokme ayar denemeleri`).
- **Bal: Shepard XSPH + 12 geçiş, tutulan meme, viskoelastik yaylar:** düz XSPH ince ipte (az komşu) neredeyse
  etkisiz, bal yağ gibi yayıldı; memeden boşluğa bırakılan katmanlar ilk anda ayrılıyordu → yeni katmanlar memede
  sabit hızla itilir, çıkınca serbest kalır. Yine de ip ya katmanlara ayrıldı (yapışmasız) ya Rayleigh-Plateau
  damlalarına bölündü (yapışmalı): PBF'te gerilmeye direnen viskoz gerilim yok. Çözüm Clavet 2005'in viskoelastik
  yayları: her yeni katman öncekine 1,6 aralıkta yaylarla bağlanır, yayın dinlenme boyu 1 sn'de uzunluğunu izler
  (akar, incelir, kopmaz), 2,5 sn sonra yay kalkar (kıvrımlar birleşip göle döner).
- **Kum: hız düzeyinde durgun sürtünme** (granüler PBD'de bilinen "uyku" eşiği). Konumsal sürtünme her alt adımda
  küçük kaymaları tamamen durduramıyor, yığın sürünüyordu.
- **Karpuz kabuğu iki katman** (yeşil dış deri ≥ 2 tanecik, açık yeşil iç kabuk), bağ dayanımına log-normal kusur
  (σ 0,6: özdeş bağlar her yerde aynı anda kırılıp kırıntı oluyordu), kaide üstü karpuzun alt yüzeyini izleyen yuva
  (düz kaidede yuvarlanıp düştü). Gerilme modülleri: kabuk 15 MPa, et 1 MPa (uyum 1/(E·aralık), her çözünürlükte aynı).
- **Kaplar kinematik tanecik:** camın yüzeyinde tek katman, beher eğilirken her adım dönüşümüyle sürülür. Hedefler
  çözücünün kendi akışında hesaplanır (varsayılan akışta yarışıyordu: canlı önizlemede su ilk karede fırlıyordu).
- **Işık:** fonun önünde, kadrajdan büyük, kenarları yumuşak düşen bir panel (sıvı fotoğrafçılığının arka ışığı), solda
  ana ışık, sağda şerit. Cam ve sıvı tam Fresnel ve kırılmayla; bardaktaki su ile cam arasındaki ince boşluk tek
  arayüz sayılır (yoksa tam iç yansıma gümüş kenar yapardı).

## Açık / bilinen sınırlar

- **Jel bloğu:** geçici boşluk görülüyor ama dar (~1 cm; gerçek 9 mm boşluğu 5-6 cm): boşluk payı 0,8'de blok masadan
  sıçrayıp döndü (aşağı giden akış masaya çarpıp geri dönüyor), 0,3'e indirildi. Esnek enerjiyi depolayan bir boşluk
  modeli (basınçla açılıp kapanan) gerek. Yoldan kopan jel tanecikleri küre damla gibi görünüyor.

- **Bal ipi** yaylarla sürekli ve yığında kıvrılıyor, ama ipin yüzeyi boğumlu (1-3 tanecik kalınlığında kafes);
  düzenli kıvrılma sıklığı (Ribe ölçeklemesi) ölçülmedi. Yaylar Jacobi ve kayan noktalı atomik: bal birebir tekrar etmez.
- **Karpuz parçaları** yüzey yeniden kurulumuyla çiziliyor: kırık yüzey keskin değil, yumuşak "jöle" kenarlı; parçaların
  üstündeki şeritler dünya konumundan (parça dönünce kayar). Patlama 7,62'de fiziksel ama çok hızlı: ×1/100'de izlenmeli.
- **Su balonu** yalnız masada; asılı balon (gerçek klasik kare) için zarın yük taşıması gerek (daha çok yineleme ya da
  daha ağır zar, ölçülmedi).
- **Kum akışı** fizik tanesi 2,5 mm olduğu için düşen akış kümecikler hâlinde (her fizik tanesine 12 ince görüntü tanesi).
- Uygulamada ince hesap görüntüyle aynı kartı paylaşıyor: su kaydı canlıdayken 2,4 kat yavaş (15,8 sn).

## NE DOĞRULANMADI

- Uygulamayı ben insan gibi kullanmadım; yalnız otomatik test (sanal fare ve tuşlar) ve komut satırı kareleri.
  Fareyle balona / karpuza tıklama yolunun isabeti ekranda görülmedi (testte düğmeler kullanıldı).
- Bal ve kum uzun süre (dakikalarca) açık kalınca tanecik sayısı kapasiteye (150 bin ek) varınca akış durur ve son
  resim kalır; bu davranış denenmedi.
- Baraj (Martin-Moyce) ve diğer T1 çözücü testleri bu geceki değişikliklerden (tane fazı, Shepard, çatlak ucu)
  sonra yeniden koşulmadı; değişiklikler varsayılan kapalı (yalnız o deneylerin ayarlarında açık).
- İnce kaydın uzun süreli bellek davranışı (art arda çok kayıt) ölçülmedi; her yeni tetik eskisini bırakıyor.

## Komut satırı

```
build\pc_graphics.exe --scene laboratuvar --still --deney su --tetik --kayit --t 1.4 --spp 64 --out captures\x.png
build\pc_graphics.exe --scene laboratuvar --seq --deney balon --tetik --kayit --t0 0 --t1 0.07 --kare 8 --out captures\x
build\pc_graphics.exe --scene laboratuvar --lab-test [balon|kum]
build\pc_graphics.exe --scene laboratuvar --test [baraj|balon|...]     (T1 çözücü testleri)
build\pc_graphics.exe --autotest-only laboratuvar
```
Ortak: `--width --height --cam "x y z" --look "x y z" --set anahtar=değer --yukseklik --akis --guc`.
`LAB_TANI=1` oturma ve akıtıcı tanısı yazar.
