# DURUM · sahne 2 "Fener"

> Tek kaynak durum panosu. Plan ve Özcan'ın tarifi `docs/fener/PLAN.md`'de.

## Şu an
- **2026-10-01 · Oyuncu kipi (merak, Özcan'ın isteği).** *"İnce detaylarla hiç uğraşma, ben mümkünatını görmek istiyorum
  [...] tab tuşuyla oyuncu-sahne arasında geçiş yapabilelim. Mevcut hali dursun [...] 3. tab da karşı adaya ışınlanalım.
  O deniz fenerinin yakından halini de görmek isterim, bir şey değiştirme."* `fener.exe` içinde **Tab**: sahne →
  oyuncu (A'da) → oyuncu (fener adası, kulenin dibi) → sahne. WASD, fare, Shift koş, Boşluk zıpla, F12 ekran görüntüsü.
  Kinematik gövde: yer yüksekliği analitik (iki ada), gövde çarpışması daire (8 m ızgara), kule ve kulübe engel, derin
  suda yüzme (göz su yüzünün 25 cm üstünde). Dünyaya dokunulmadı. Ölçüldü (`--player-test`): zıplama 1,16 m (hesap 1,17),
  koşu 8 sn'de 61 m (gövde sürtünmesiyle), kuleye yürüyünce 5,05 m'de durur, koyda yüzer; pencerede `--autotest-player`
  Tab/zıpla/ışınlan/geri dön akışı 88-108 fps. Kareler `docs/fener/kareler/oyuncu_*`.
- **2026-10-01 · Sahne 2 kapandı.** Özcan: "Çok güzel oldu. Görev tamamlandı." Açmak: `build\fener.exe`. Sanat cilası
  (aşağıdaki "Açık" maddeleri) yapılmadan kapandı; istenirse açılır.
- **2026-10-01 · F2 + yolculuk çalışıyor.** Kuşbakışı harita (`--map`), A ve B kilitli, Space ile A → B orman içinden
  yolculuk (778 m, ~70 sn, yumuşak kalkış/duruş, yolda tekrar Space geri döner). Kareler `docs/fener/kareler/F2_*`.
  Açık: yakın köknar kalitesi (karton testere görünümü), ormanda huzmeyle beyaza dönen iğneler, çisenti, göz kamaşması
  yıldızı (3B'de henüz yok), canlı modda huzme gürültüsü ölçülmedi, kulübe A'dan çok küçük.
- **2026-10-01 · F1 altyapı bitti, ilk 3D kareler çıktı.** `pc_graphics.exe --scene fener` (pencere) ya da
  `--headless --frames 256 --out ...` (durağan kare) / `--headless --live N` (canlı ölçüm). Sahne 1 bit bit aynı
  (değişiklikten önce ve sonra 960x540 32 örnek, `--seed 7`: piksel farkı 0). Kareler `docs/fener/kareler/F1_A_t0.png`, `F1_A_t5.png`.
- **2026-10-01 · Özcan seçti: A başlangıç, B varış, Space ile canlı yolculuk orman içinden** (PLAN "F0 sonrası
  kararlar"). Tepe anı: ışık sütunu + göz kamaşması. Sıradaki: F1 altyapı.
- **2026-10-01 · F0 değer etüdü bitti.** Üç kompozisyon + "huzme kamerada" anı:
  `docs/fener/etud/` (A pencere, B yücelik, C yakın; A'nın tepe anı). Üretici: `python tools/fener_etud.py --variant all --w 1600`
  (`--phi 2` huzmeyi kameraya çevirir). Taslak başına ~1 dk (1600 px, numpy).

## Fazlar
| Faz | Durum | Not |
|---|---|---|
| F0 Değer etüdü | ✅ 10-01 | A + B seçildi (C hayır); tepe anı: ışık sütunu + göz kamaşması |
| F1 Altyapı (sahne seçimi) | ✅ 10-01 | ayrı modül `src/fener/`, cihaz programı `src/device/fener.cu`, ayar `scenes/fener.cfg`; sahne 1 bit bit aynı |
| F2 Kaba 3D + harita + yolculuk | ✅ 10-01 | B kuzeydoğu burnuna taşındı (920 m, sağı açık deniz); A ve B'nin önüne koy oyuldu; fiyort duvarları yay; orman: kenarda etekli, içeride gövdeli köknar |
| F3 Işık | ⏳ | |
| F4 Su | ⏳ | |
| F5 Son işlem | ⏳ | |
| F6 Hareket, döngü, video | ⏳ | |
| F7 Sanat turları | ⏳ | |

## Mimari (F1)
- Ayrı modül, sahne 1'e dokunmaz: `src/fener/` (uygulama, OptiX, sahne üretimi), `src/shared/fener_params.h`,
  `src/device/fener.cu` (NVRTC, F5 ile yeniden derlenir). `main.cpp` yalnız `--scene fener`'i yönlendirir.
- Playdead'in tarifi: az örnek + mavi gürültü + zamansal birikim (TAA). Sahne hiç durağan değil (huzme döner), o
  yüzden "dur ve topla" yerine her kare kendi başına iyi olmalı.
- Geçişler: yüzey (tam çözünürlük; ay, huzme, pencere ışığı, gölgeler, gök örtüsü, deniz) → hacim (yarım çözünürlük;
  huzme eşit açılı, ay geçirgenlikle örneklenir, gölgeli) → hacmin zamansal birikimi → birleştirme (derinliğe duyarlı
  büyütme) → TAA (YCoCg sapma kırpması) → iki parlama piramidi (kernels.cu `launchBloom`) → son geçiş (ACES, soğuk
  gölge, TPDF mavi gürültü titreşimi).
- Dünya: RGB (tayfsal değil), deniz analitik düzlem (40 dalga, Smith maskeli Beckmann parıltıları), köknar 6 çeşit
  x ~32 bin örnek (253 milyon üçgen örneklenmiş), adalar yükseklik alanı, fiyort duvarları perde.

## Ölçümler (F1, RTX 4080)
| Ne | Değer |
|---|---|
| canlı 2560x1440, huzme 32 + ay 8 örnek/piksel | 6,2 ms/kare (160 fps): yüzey 1,6 · hacim 2,9 · TAA 0,5 · parlama 0,45 ms |
| canlı 2560x1440, huzme 8 örnek | 4,6 ms/kare (220 fps) |
| durağan 2560x1440, 256 kare birikim | 1,1 s |
| sahne kurulumu | 14 ağ, 31 957 örnek, ~0,1 s |

## F1'de öğrenilenler (ölçülerek)
- **Hareket vektörü titreşimli noktadan hesaplanınca TAA görüntüyü bulanıklaştırır:** durağan kamerada bile geçmiş her
  karede alt-piksel kaydırılıp yeniden örneklendi. Aynı dünya noktasının iki kameradaki titreşimsiz izdüşümü alınınca
  keskinleşti.
- **Denizde ufuk lekeleri:** Beckmann parıltısında Smith maskeleme yoktu; sıyırma açısında 1/(4 cos) şişiyordu.
- **Yüksek pus 40 km boyunca sayılınca bütün gök sis rengine boğuldu;** pusa ~2 km ölçek yüksekliği verince gece göğü
  yukarıda göründü.
- 32 karede hacimdeki benekler sistematik değil: 256 karede pürüzsüz (ölçüldü). Canlıda örnek sayısı artırılacak.

## F2'de öğrenilenler (ölçülerek)
- **Harita ilk denemede iki tasarım hatası gösterdi:** B'nin görüş koridoru kıyı boyunca bütün ormanı silmişti; ve B
  kıyının fenere bakmayan yerindeydi (bakış çizgisi ~150 m kara üstünden). Kuşbakışı harita aynı yükseklik
  fonksiyonları ve aynı ağaç örnekleriyle çizildiği için (aynı kaynak) bunlar 3B'den önce görüldü.
- **Kompozisyon ölçüsü:** A'da kıyı 34 m ilerideyken karanın düz yüzeyi kadrajın alt %28'ini yuttu; 1,7 m göz
  yüksekliğinde kadrajın alt ~%12'si için kıyı ~10 m ileride olmalı. Çözüm: bakışın önüne koy oyuldu.
- **Sık ormanın içi gerçekçi olarak karanlıktır:** açık bakışlara göre 4,8 EV (log2 parlaklık A -7,0, B -7,4, orman
  -12,0). Göz uyumu eklendi (farkın yarısı, 1,5 sn): orman yine karanlık hissettirir ama okunur.
- **Yarım çözünürlüklü hacim yakın silüetlere parlak hale bindiriyordu** (derinliğe duyarlı büyütme de kurtarmadı);
  hacim tam çözünürlüğe alındı: 1440p'de 7,6 ms/kare (131 fps).
- **Yer pusu (7 m ölçek yüksekliği) ormanın içini kurdu:** gövdeler parlayan pusun önünde silüet olunca Inside okunur.
- **Tepe anı 3B'de kendiliğinden bir şey üretti:** galeri balkonu aşağı inen ışığı kesiyor, lambadan suya karanlık
  bir gölge piramidi düşüyor; kubbe yukarıya karanlık bir V çiziyor. Özcan'ın gözüne sunulacak.

## Etüdün sayıları (seçilen taslak 3D'ye bunlarla taşınır)
| | A Pencere | B Yücelik | C Yakın |
|---|---|---|---|
| kamera yüksekliği / bakış yukarı / dikey FOV | 5 m / 5° / 38° | 4 m / 10° / 44° | 7 m / 4° / 34° |
| adaya uzaklık / tepe yüksekliği | 470 m / 44 m | 900 m / 46 m | 330 m / 40 m |
| ön plan | iki yakın köknar + ince ölü gövde | tek köknar solda, sağ açık | iki köknar + ince gövde |

Ortak: fener kulesi 36 m (taban yarıçapı 4,4 m, tepe 3,1 m), lamba ~tepe + 39 m; kulübe fenerin 74 m sağında,
22 m önde, sundurmalı ve hafif yamuk; fiyort duvarları 6 / 10 / 16 km'de, 0,25-5 km yüksekliğinde; ay sol üstte.

## Etüt sırasında öğrenilenler (ölçülerek)
- **Kör ayar döngüsü pahalı:** tepe anı dört deneme boyunca "her yer beyaz" kaldı; bileşenleri tek tek ölçünce suçlu
  sis değil, benim eklediğim parlama yıldızının geniş halkasıydı (sisin ~20 katı). İlk iş ölçmek olmalıydı.
- **Gerçek fener huzmesi kameraya çarpmaz:** lamba ~82 m yüksekte, 470 m'de kamera eksenin ~9,7° altında. Yatay ve dar
  bir huzme hep üstten geçer. Çözüm (sanat): huzme gerçek gibi neredeyse yatay kalır ve kameranın üstünden geçer
  (tepede lambaya yakınsayan ışık sütunu); "kameraya vuran ışık" = huzmenin yönü kameraya döndüğünde lambanın
  göz kamaştırması.
- **Kameranın içinde olduğu huzme bütün sisi yakar** (ileri saçılım fiziksel olarak ~100 kat). Gerçekçi ama düz ve
  güzel değil; ön/yan oranı ~5'e indirildi.
- **Değer ağı:** dalgalar değer gürültüsüyle (ızgara) yapılınca blok ve yatay şerit verdi; rastgele yönlü 40 dalganın
  toplamı (2. harmonikle sivri tepe) temiz kırık yansıma verdi.
- **Fiyort duvarları ölçekle görünür oldu:** 5-14 km'de 0,2-2,6 km yükseklik, adanın ormanının arkasında kalıyordu;
  6-16 km'de 0,25-5 km'ye çıkınca ayın altında beliriyor.

## Bilerek fizik dışı (sanat) ayarları
Etütte (3D'ye taşınırsa aynı adla işaretlenecek):
- huzme saçılımında ön/yan oranı ~5 (gerçek sis ~100), ve huzmenin sisteki parlaklığına yumuşak tavan (`beamCap`);
- huzmeye ek zayıf, geniş, aşağı eğik yayılma lobu (`spill`): ormanı ve denizi süpürsün diye;
- göz kamaştırma yıldızı huzmenin **yönüne** bağlı, kameradaki gerçek şiddete değil (`face()`);
- ağaç tepelerinin huzmeden aldığı ışığa yumuşak tavan; ay diski büyütülmüş (0,55°, gerçek 0,26°).
