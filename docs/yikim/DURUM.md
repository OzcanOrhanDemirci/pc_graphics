# DURUM · sahne 3 "Yıkım"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/yikim/PLAN.md`'de.

## Şu an
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
- **Normal kılavuzunun uzayı:** OptiX 9.1 başlığı (`OptixDenoiserGuideLayer`) AOV modelleri için normali **dünya**
  uzayında istiyor; sahne 1'in hazırlığı kamera uzayına çeviriyor (kaynak gösterilerek). Sahne 1'de doğrulanmadı.

## Deprem kayıtları (`assets/deprem/`, `src/yikim/quake.*`)
ESM (esm-db.eu, işlenmiş MP, hesapsız indirilebilir) ve AFAD TADAS (misafir girişi). İvme cm/s², hepsi ayrıştırılıp
PGA başlıkla karşılaştırıldı. Kullanılacaklar: **Pazarcık TK.4614** (2023, ESM MP, PGA 2,25 g; ham AFAD kaydında
6,8 cm/s² sabit kayma var, sürmek için ESM işlenmişi), Antakya TK.3123 (2023), Yarımca YPT ve İzmit TK.4101 (1999),
Bayraklı TK.3513 (2020, AFAD mp). Atıf: ESM (Mascandola ve diğ. 2026, doi 10.13127/esm.3) ve AFAD TADAS.
İki kez integre (yamuk), işlenmiş kayıtlarda hız ve yer değiştirme sonda sıfıra döner (Pazarcık PGD 0,5-0,6 m, PGV
0,93 m/s; Yarımca PGD 0,81 m). Zemin her karenin sonundaki konuma gider, alt adımlarda doğrusal; kayıt `deprem.offset`
saniyesinden, 1 sn'de yumuşak girişle başlar. Not: uygulamanın yazdığı PGA yatay vektör tepesi (Pazarcık 3,0 g;
bileşen tepesi 2,25 g).
