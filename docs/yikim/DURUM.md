# DURUM · sahne 3 "Yıkım"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/yikim/PLAN.md`'de.

## Şu an
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
| Y1 Bina | ✅ benim tarafım | apartman, testler, üç klip; kesit ısı haritası yapılmadı (tanı araçları yetti); Özcan'ın gözü bekleniyor |
| Y2 Deprem | ⏳ | kayıtlar hazır (aşağıda) |
| Y3 Kırılma görünümü | ⏳ | |
| Y4 Toz | ⏳ | |
| Y5 Görüntü | ⏳ | |
| Y6 Canlı | ⏳ | |
| Y7 Fizik turları | ⏳ | |

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
- **Temas: çift başına bir warp.** Bir cismin örnek noktaları (yüze 5 mm içeri çekilmiş köşe, kenar ve yüz noktaları)
  ötekinin düzlemlerine karşı. Her yinelemede noktaların normal düzeltmesinin ortalaması, sonra yamanın merkezinde tek
  sürtünme; 4 yineleme. **Sürtünme çapası** (PhysX gibi): ilk temasta yama merkezi iki cisme iğnelenir, statik sürtünme
  iğneleri μs·N bütçesiyle birleştirir; tutamazsa kayar (bütçe μd·N, iğneler taşınır). Hız geçişi yalnız kalan
  bütçeyi harcar; çarpma katsayısı ve burulma sürtünmesi yama merkezinde. Cisimler arası Jacobi (çift sayısına bölünür).
- **Alt adım döngüsü bir CUDA Graph**; ayar ya da dt değişince yeniden yakalanır. Zemin = 0. cisim (kinematik),
  her alt adımın zemin konumu cihaza yazılır (deprem için hazır).

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

| Kayıt (×1) | Yatay tepe | Sonuç |
|---|---|---|
| 2020 Bayraklı | 0,12 g | ayakta, %0,6 öteleme, korkuluklar ve bazı duvarlar dökülür |
| 1999 İzmit | 0,25 g | ayakta (%0,3); düzeltmeden önceki bir koşuda kısmi göçme |
| 1999 Yarımca | 0,34 g | **sınırda:** 3 koşunun 2'sinde tam göçme (atomik toplamlar yüzünden deterministik değil) |
| 2023 Antakya | 0,67 g | tam göçme |
| 2023 Pazarcık | 2,25 g (bileşen) | tam göçme |

Kolon kaldırma (zemin kat): 2 köşe kolonu → ayakta; ön hattın 4'ü → ayakta (üst katlar dolgu + çerçeve kafes kutu,
döşeme donatısı çekme başlığı); ön iki hattın 8'i → ön yarı göçer.

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

## Deprem kayıtları (`assets/deprem/`, `src/yikim/quake.*`)
ESM (esm-db.eu, işlenmiş MP, hesapsız indirilebilir) ve AFAD TADAS (misafir girişi). İvme cm/s², hepsi ayrıştırılıp
PGA başlıkla karşılaştırıldı. Kullanılacaklar: **Pazarcık TK.4614** (2023, ESM MP, PGA 2,25 g; ham AFAD kaydında
6,8 cm/s² sabit kayma var, sürmek için ESM işlenmişi), Antakya TK.3123 (2023), Yarımca YPT ve İzmit TK.4101 (1999),
Bayraklı TK.3513 (2020, AFAD mp). Atıf: ESM (Mascandola ve diğ. 2026, doi 10.13127/esm.3) ve AFAD TADAS.
İki kez integre (yamuk), işlenmiş kayıtlarda hız ve yer değiştirme sonda sıfıra döner (Pazarcık PGD 0,5-0,6 m, PGV
0,93 m/s; Yarımca PGD 0,81 m). Zemin her karenin sonundaki konuma gider, alt adımlarda doğrusal; kayıt `deprem.offset`
saniyesinden, 1 sn'de yumuşak girişle başlar. Not: uygulamanın yazdığı PGA yatay vektör tepesi (Pazarcık 3,0 g;
bileşen tepesi 2,25 g).
