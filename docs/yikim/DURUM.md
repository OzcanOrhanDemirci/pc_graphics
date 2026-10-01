# DURUM · sahne 3 "Yıkım"

> Tek kaynak durum panosu. Plan ve Özcan'ın sözleri `docs/yikim/PLAN.md`'de.

## Şu an
- **2026-10-01 · Y0 sürüyor: fizik çekirdeği çalışıyor, testler 15/17.** Görüntü ve klipler henüz yok.
  `build\yikim.exe --test` (ya da `--test dusme|yigin|egim|konsol|enerji|cerceve`), ayarlar `scenes/yikim.cfg`,
  `--set anahtar=deger`; tanı için `--set test.verbose=1`, `--set test.profile=1`, `--set test.elastic=1`.

## Fazlar
| Faz | Durum | Not |
|---|---|---|
| Y0 Fizik çekirdeği | ⏳ | çözücü + testler ✅; sade görüntü ve iki klip (kolon kaldırma, sarsıntı) sırada |
| Y1 Bina | ⏳ | |
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
| **konsol kiriş 5 m, 10 parça** | **sehim 2,7×, frekans 0,61× analitik: KALDI** | 0,7..1,5 |
| fizik süresi, çerçeve | 5,0 ms/kare (40×2); 20×1'de 1,5 ms | |

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

## Deprem kayıtları (Y2 için, `assets/deprem/` olacak; şimdilik oturum karalama klasöründe)
ESM (esm-db.eu, işlenmiş MP, hesapsız indirilebilir) ve AFAD TADAS (misafir girişi). İvme cm/s², hepsi ayrıştırılıp
PGA başlıkla karşılaştırıldı. Kullanılacaklar: **Pazarcık TK.4614** (2023, ESM MP, PGA 2,25 g; ham AFAD kaydında
6,8 cm/s² sabit kayma var, sürmek için ESM işlenmişi), Antakya TK.3123 (2023), Yarımca YPT ve İzmit TK.4101 (1999),
Bayraklı TK.3513 (2020, AFAD mp). Atıf: ESM (Mascandola ve diğ. 2026, doi 10.13127/esm.3) ve AFAD TADAS.
