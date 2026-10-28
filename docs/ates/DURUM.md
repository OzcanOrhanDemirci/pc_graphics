# DURUM · sahne 12 "Ateş"

> Plan `docs/ates/PLAN.md` (hedef, kararlar, mimari); varlıkların kaynağı ve lisansı `docs/ates/VARLIK.md`. Hafızada
> görev kartı `grafik/project_ates_gorevi.md` (Özcan'ın brief'i birebir). Bu dosya: nerede olduğumuz, ne ölçüldü, ne
> açık. Sabitlerin kaynağı başlıklarda, her sabitin yanında (`ates_gas.h`, `ates_wood.h`, `ates_sparks.h`).

## Özcan: nasıl açılır, ne denenir

- Varlıklar git'te değil: önce `python assets/ates/fetch.py` (beş CC0 varlık, ~70 MB). DLSS için `third_party/fetch.sh`.
- `build\pc_graphics.exe` → galeride **0**, sonra **iki kez aşağı ok** (12. sahne) → Enter. Fare bakar (Tab imleci bırakır),
  **W A S D** yürür, **C** çömelir. **R** ateşi baştan yakar, **1-4** bakışlar, **↑ ↓** pozlama. (Kibrit, odun atma,
  üfleme ve zaman hızlandırma kapanışta çıkarıldı: §1.)
- Komut satırı: `build\pc_graphics.exe --scene ates --view 3 --sure 10 --out <yol>` (resim), `--bench 240 --canli`
  (kare süresi; `--yeniden 50`: her 50 karede bir R), `--senaryo kendi` (§3; eğri `<yol>.csv`, resimler
  `<yol>_<t>s.png`), `--set debug.wood=1` (kütük kütük ısı dengesi). Görüntünün temizliği: `--seri 24 --dondur [--donus 12]` (ateş durur, yalnız örnekler
  kıpırdar; sonunda DLSS'siz referans `ref.png`, dönerken her kare için `ref_*.png`), sonra `python tools\siyah.py
  <yol>` (gren, sapma, siyah) ve `python tools\titreme.py <yol>` (canlı/referans titreme oranı).

## 1 · Şu an (2026-10-28 öğlen)

Plan onaylandı (Özcan, 10-28: *"sahneyi tamamlayana kadar çalışmaya devam et [...] tam yetkilisin"*). F0-F5'in kodu
var: gaz, odun, kor, kıvılcım, ışık, etkileşim, zaman hızlandırma. F6'dan canlı dumanın çizimi var ama kapalı (§5),
pişmiş uzak tüy yok; F7 (kapanış ölçüleri, kör test) yok.

**Bu sabahın asıl bulgusu: ateş kendi kendini sürdürmüyordu, sürdürüyor görünmesi bir hataydı.** Gaz ızgarasında bir
kütüğün içine düşen hücre, o anki gazını (yanan yüzün dibinde, uçucuca zengin) donmuş hâlde tutuyordu; komşuları her
adımda ondan örnekliyor, yakıt hiç tükenmiyordu. Ölçü: 60 kat hızda 0,4 g/s yakıttan 40 kW ısı (yakıtın verebileceği
~6 kW). Hata kapatılınca (`kGhost` + duvarda örnekleyenin kendi değeri) ateş gerçek zamanda iki dakikada söndü.
Sönmenin sebepleri tek tek ölçülüp fizikle kapatıldı (§2). Bugünkü hâliyle ateş gerçek zamanda yanmayı
sürdürüyor (yakıt defterinden sonra ~7,6 kW), 60 kat hızda ~45 dakikada (odun zamanı) yanıp sönüyor; senaryoların
üçü tam, ikisi yarım, ikisi yürümüyor (§3).

**İyileştirme turu (PLAN §7):** İ1 temiz siyah ✅ (§2, §4): gök her pikselde 0, canlı görüntü çok örnekli
referansı kadar sakin.

### Kapanış (2026-10-28 akşam, Özcan'ın kararı)

Özcan: *"Açıkçası bu ateş sahnesi beklediğimden çok maliyetli hale geldi. [...] sahneyi sorunsuz çalışır hale getir.
Bozukluk olmasın. Sonra bitirelim. Çalışmayan özellikleri ise silelim. [...] Ateşin görüntü olarak gerçek olmasını
yapmış olduk. İlave yanması sönmesi o su bu su olmamış olur."* Haftalık kotanın ~%10'u bu sahneye gitti.

Sahne **bakılan bir ateş** olarak kapandı: alev, ışık, kıvılcım, kor ve gecenin karanlığı fizikle, gerçek zamanda.
Çıkarılanlar ve nedenleri:

| Çıkan | Neden (ölçülmüş) |
|---|---|
| **F** kibrit (ve söndükten sonra yeniden yakma) | `tutusturma` ❌: 0,2 kW'lık parlama, alev çıraya geçmiyor (PLAN §7 İ3, İ5) |
| **Q** çalı, **E** kütük | `besleme` sonucu atılan kütüğün nereye yuvarlandığına bağlı (aynı kodla 4/4 söndü, bir önceki commit'le 3/3 sürdü); çökmenin geçtiği yol (§2) |
| **Sol tık** üfleme | `ufleme` ◐: kor harlanıyor ama gözle seçilmiyor, alev büyümüyor (İ4) |
| **T** zaman hızlandırma | `kor`: 45. dakikada alev ve kor birlikte bitiyor, kütükler yerinde kararıyor; alevsiz kor evresi yok (İ2) |
| `soguk` ve `kor` yerleşimleri, beş senaryo | yalnız yukarıdakileri sınıyorlardı |

Kalan ateş kendi hâlinde gerçek zamanda ~8-9 kW'ta yanıyor (§3); odunun 60 kat hızlı ölçümünde ~45 dakikada
sönüyordu (gerçek zamanda bu süre ölçülmedi). **R** onu baştan yakar. Gaz, odun, kor ve kıvılcımın motoru değişmedi.

## 2 · Bu oturumda bulunan ve düzeltilen fizik

| Bulgu (ölçüyle) | Düzeltme |
|---|---|
| Katı hücre yakıt kaynağı: ısı salımı beslemenin 7 katı | Katı hücre gaz komşularının ortalamasını alır; adveksiyonda katı köşe örnekleyenin kendi değerini verir |
| Kömür yatağı kule olup devriliyor: 30 kömür 37 cm'lik sütun, 0,67 m'ye saçılıyor | Yatak kütüklerden önce, kalınlığı kadar uzun parçalarla (40 parça, r 1-2 cm) |
| Kömürün kendi tepkime ısısının %30'u gaza veriliyordu (taşınımla bir kez daha kaybediliyordu) | Yüzey tepkimesinin ısısı yüzeyde; CO'nun sınır tabakasında yanan payı (850-1000 K) yarı yarıya |
| Kendi alevi altındaki kütük gazdan ~1 kW/m² alıyordu (odun alevi yakıtına 15-30 verir) | Izgaradan ince alev tabakası: yanan hücre yüze bitişikse 20 W/m²K × (1800 K − yüzey) |
| Kül yatağı korların sıcak payının 3/4'ü (soğuk yatak korları soğutuyordu) | Üstündekilerle ışınım dengesinde, ışınımın onda biri toprağa |
| Üfleme 0,8 m öteden: korlara ~1,5 m/s, kor yanması %13 düştü | Ağız baktığın şeyin 30 cm önünde: kor yanması +%17, en sıcak yüzey +60 K |
| Kibrit hiçbir şeyi tutuşturamıyordu: 80 W bir hücreye dağılıp 5 K ısıtıyordu; soğuk dizilişte çıra yoktu | Kibritin başı ızgaradan ince kendi alevi (yakıtı havasıyla gelir, yerinde yanar); dizilişe 1 mm'lik talaş; kibrit yerden 1,5 cm |
| Kül yatağı ölçüsü yerine ışınım dengesi (A/B, 60 kat hız) | 5. dakikada 6,2 → 10,1 kW; alevler ~25 yerine ~45 dk sürüyor |
| Kıvılcımların üçte biri doğduğu an katıda ölüyordu (241/639) | Katıdan boş yana adım, yüzden sekme (12/628) |
| Gaz yakıtından fazlasını yakıyordu: brülör testinde 20 kW'tan 31,5 kW (alevin genleşmesinde yarı Lagrange taşıma korunumsuz); odun ateşinde 1,4 kat | Yakıt türlerinin (uçucu, CO, is, duman) defteri: hücrelerin tuttuğu ve genleşmesinin ittiği, ızgaradan çıkan, taşımadan sonra kalan; fark bütün ızgaraya geri ölçeklenir. Brülör 19,7 kW, girdi 4,11 g → hesaplanan 4,09 g |
| Odun atarken çökme (Özcan, 10-28): her atışta bütün kütüklerin ışın izleme yapısı yeniden kuruluyor, OptiX'in kurucusu arada geçersiz adrese düşüyordu (çalı ile 3/3) | Atılan kütüğün yapısı tek başına; diğerleri biçimi değişince (en çok 8). 40 atışlık zorlamada 6/6 geçti |
| Ateşin üstünde izleyen siyah kareler (Özcan): DLSS yarım duyarlıkta 65504'ü aşan parlaklığı (alevin dibinde 1e4-1e5 cd/m²) ve sonlu olmayan rehberleri geçmişinde taşıyor | Renk pozlamayla ölçeklenip veriliyor (InPreExposure); sonlu olmayan her değer türüyle sayılıp ayıklanıyor. Ölçümde kare görülmedi, canlı denemede doğrulanacak |
| Galeride ateşin resmi yok, yükleme ekranında çıplak geçiş bantları | `assets/gallery/ates.jpg` (alçak bakış) |
| Duman hiç azalmıyordu: 10 sn'de 0,39 g, bir hücrede 104 g/m³ (soğuk yüzlerin dibinde sis) | Yüz dibinde yoğuşmaz; sıcak gaza dönen damlacık buharlaşıp yanar; yüze çöker (1 cm/s): 0,04 g, en yoğun 24 g/m³ |
| İ1 · Siyahta gren (Özcan): titretme sRGB eğrisinden önce, doğrusal ışıkta; siyahın yanında eğrinin eğimi 12,92, ±0,5 seviye ±6,5 oluyordu; desen piksel yerinin doğrusal karması (çizgiler). Gökün %46'sı 1-6 seviye | Sabit TPDF mavi gürültü titretmesi (±1 seviye, Köy'deki gibi) kod değerine, eğriden sonra; siyaha doğru son seviyede söner (gerçek siyahta basamak yok, 0'daki kırpma onu %12,5 serpintiyle kaldırıyordu). Gök %100 sıfır (`eec4add`) |
| İ1 · Klipler (`--seri`) canlıdan ~4 durak parlak: otomatik pozlama kapanınca pozlama 1'e düşüyordu (canlıda ×0,046), DLSS geçmişi ~20 karede yetişiyordu | Kapalı pozlama son uyum değerinde kalır (`6d160c0`). Bundan önceki kliplerin parlaklık ve sapma ölçüleri geçersiz |
| İ1 · "Fırça izi": zeminin kendi dokusu; sıyırarak gelen ışıkta 16 alt örnek × 64 ışık örnekli referansta da aynı | Değişiklik yok. Pikselde 4 ışık örneği (`light.samples`) 1 ile aynı görünüyor: 1 kalıyor |
| Kapanış · Uygulama hâlâ ara sıra ölüyordu: bugün 23 senaryo koşusunun 4'ü; mesajı kalan ikisi "an illegal memory access", `buildGas`'ta, yarım saniyede bir şekli değişen kütüklerin (en çok 8) yapısı yeniden kurulurken (kök neden: §7) | Yapılar yalnız ateş yakılırken (açılış, R) kurulur ve kalır (`282c363`); gözün gördüğü biçim yanmayı izlemiyor, ateşin saatinde birkaç milimetre. Kararlılık §4 |

Yakıt defteri (`2fb24fb`) gelince ateşin gücü dürüstleşti: kendi hâlinde gerçek zamanda ~7,6 kW'ta duruyor (önce 14,7;
fazlası taşımanın yarattığı yakıttı). Brülör testinin eski "%90"ı tesadüftü: brülörün 450 K'lik yakıtı katran olarak
yoğuşup kayboluyor, taşımanın fazlasını örtüyordu.

## 3 · Senaryolar (`--senaryo <ad>`, PLAN §2.1)

Kapanışta yalnız `kendi` kaldı (§1); öteki altı satır, çıkarılan özelliklerin son ölçüsü olarak tarih. Göz ocağın
açık yanından (kütükler x boyunca), kömürlere bakarak çömelmiş.

Ölçüm 2026-10-28 öğlen, yakıt defteriyle (`2fb24fb`). HRR: gazın ısı salımı; "kor": 800 K üstü yüzey. Akşam aynı
senaryolar yeniden koşunca `besleme` ve `bogulma` farklı çıktı (besleme 4/4 söndü, bogulma 13. dakikada 15,8 kW): §7.

| Senaryo | Beklenen (brief) | Çıkan | Hüküm |
|---|---|---|---|
| `kendi` (gerçek zaman, 3 dk) | yanmayı sürdürür | 22 → 7,6 kW, 2. dakikadan sonra düz; piroliz 0,59'a inip 0,70 g/s'ye döner; kor ~2450 cm². **Kapanışta** (`282c363`, iki koşu): sonda 8,7 ve 9,3 kW, 2,72 ve 2,71 kg odun, kor 2574 ve 2752 cm² | ✅ |
| `ufleme` (15 sn üfleme) | rüzgâr yönüne harlanır, güçlenir | kor yanması %31 artar (0,172 → 0,226 g/s), en sıcak yüzey +15 K; alevin HRR'si düşüşünü sürdürür (19,8 → 16,2 kW) | ◐ kor harlanıyor, alev güçlenmiyor |
| `kor` (odun 60 kat) | yanar, azalır, son noktada alevsiz kor | ~45 dk yanar (8 → 3 kW), 55. dk'da alev ve kor birlikte biter; 1,53 kg odun kalır | ◐ alevsiz kor evresi yok |
| `bogulma` (6 kütük, 8 kg) | oksijeni kesilir, zayıflar; kor sıcaksa toparlanır, değilse söner | 19,7 → 14,8 kW, sonra (odun 15 kat) 3. dakikada 2,9 kW'a, kor 2800 → 360 cm²: boğulup içten içe yanar, 13. dakikada hâlâ 1,5 kW | ✅ boğulma |
| `besleme` (5 dk'da bir kütük) | beslendikçe sürer | yarım saat 7-21 kW; atılan her kütük ateşi bir süre büyütür | ✅ |
| `yeniden` (35 dk sonra 15 sn üfleme) | kordan yeniden alev | kor 1530 K'e çıkar, kor yanması %35 artar; alev dönmez (~1 kW) | ❌ |
| `tutusturma` (kibrit) | kibrit → çıra → çırpı → kütük | çıra 1350 K, 0,1-0,2 kW'lık parlama; kibrit bitince söner | ❌ |

## 4 · Ölçümler

| Ne | Değer | Nereden |
|---|---|---|
| Brülör (20 kW, D 0,4 m, 20 sn) | HRR 19,7 kW, ışınım payı 0,20 (odun alevi ~0,2-0,3) | `--gaz 2400` + `tools/ates_olc.py`, `2fb24fb` |
| Alev boyu | %95 ısı salım yüksekliği 0,39 m; Heskestad 0,37 m (uç medyanı 0,71 m: tek tük dil) | aynı |
| McCaffrey ekseni | sürekli alevde 1550 K (McCaffrey ~1100), tüyde aşırı sıcaklık 2 kat; hız 1,3-1,5 kat | aynı |
| Kabarma (puf) | uçta 1,29 Hz; 1,5/√D = 2,37 Hz | aynı |
| Kare, bakış 0 (1440p, DLSS-RR DLAA, canlı) | 38,1 FPS, 26,3 ms (en kötü 29,8); gaz 15,4 (3 adım × 5,2), DLSS 5,9, ateş 1,1, odun 0,9 ms | `--bench 240 --canli`, `2fb24fb` |
| Kare, bakış 3 (közün içine) | 33,7 FPS, 29,7 ms; ateşin geçişi 5,2 ms; gaz gerçek zamanın %84'ü | aynı |
| Gaz adımı | 4,6-5,0 ms (128×192×128, 7,5 mm) | `--gaz`, `--bench` |
| Kıvılcım | 10 sn'de ~630 doğan; adım + çizim 0,2 ms | §2 |
| Siyah gök (son kare, üst 300 satır; `--seri 6 --dondur --sure 2`) | eski titretme: %54 sıfır, kalanı 1-6 · mavi gürültü: %87,4 sıfır, %12,6 bir · siyahta sönen: **%100 sıfır** | `eec4add` |
| Titreme (ateş donmuş, bakış 0 dönüyor, 24 kare; `tools/titreme.py`) | 1°/s (~1 px/kare): canlı 0,0028, referans 0,0030, **oran 0,94** · 12°/s: 0,0150 / 0,0146, **oran 1,03**. (Orman'ın DLSS'li 0,0047'si dolu karede yürürken: aynı ölçü değil; oran Kuyu'daki gibi ~1) | `f06675a` |
| Loş zemin, canlı / referans (son kare, `--seri 6 --dondur --sure 2`, dört koşu) | referansta 1-2 seviye −28…−42 %, 2-4 −10…−31 %, 8-16 −9…−26 %, 32-80 0…−7 %, 80+ 0…−2 %: DLSS-RR loşu karartıyor, koşudan koşuya çok oynuyor | `eec4add` |
| Uzak zeminde ışık örnekleri (`debug=4`) | ~%55'i yüzeye ulaşıyor, kalanı ufkun altından ya da gölgede | `a5fcf02` |
| **Kararlılık (kapanış)** | 4 canlı koşu × 4000 kare (`--bench 4000 --canli`), 3 × 2000 kare her 50 karede R (`--yeniden 50`: 120 yakma, her biri 43 yapı), 2 × `--senaryo kendi`: **çökme 0, CUDA hatası 0, sonlu olmayan değer 0** | `282c363` |
| Kare, kapanışta (1440p, DLSS-RR DLAA, canlı) | 40,0-40,7 FPS, ~24,8 ms, en kötü 29,8-38,8 ms (güncelleme 16,3, görüntü 8,5 ms); R zorlamasında 38,6-40,0 FPS. Uygulamada (`--autotest-only ates`) dört bakış 35,6-39,4 FPS | `282c363` |

## 5 · Açık işler

**Yok: sahne kapandı (§1).** İyileştirme turunun İ1'i bitti; İ2-İ9 Özcan'ın kapsam kararıyla yapılmayacak (PLAN §7,
§8). Sahneye dönülürse yol haritası olarak PLAN §7 duruyor.

**Özcan'ın kararı (açık bırakıldı):** loş zemin canlıda referansından %10-30 koyu (§4, DLSS-RR'nin loş, seyrek
örneklenmiş yerdeki yanlılığı). Netlik kuralı koyuluğa kendiliğinden dokunmayı yasaklıyor.

## 6 · Kararlar (Claude, tam yetkiyle)

- Davranış kuralla yazılmaz: yukarıdaki her düzeltme bir ölçülmüş eksikliğin fiziği. İki ızgara altı model kondu,
  ikisi de bilinen fiziğin kısaltması: kömürün çift film yanması (büyük kömür taneciğinde CO sınır tabakasında yanar)
  ve yanan yüze bitişik alev tabakasının iletimi (k/δ). Sabitleri `ates_wood.h`'de gerekçesiyle.
- Kütük ışığı fizikle tutarlı (ρ·L·Ω/π); görünümde desene ve pürüze dokunuldu, ışığa değil (önceki oturum).
- Parlama (bloom) 0,05 → 0,02: kütükleri altın bir pus örtüyordu. Ton eşleme anahtar 45, kontrast 1,5, doygunluk 1,2.
- Alevin mavisi tayftan: CH* 431, C2 Swan 473/516/563, CO2* sürekli; elle renk yok.

## 7 · NE DOĞRULANMADI

- Gerçek bir kamp ateşiyle kör test yapılmadı; ateşin "gerçek" göründüğü Claude'un gözü, ölçü değil.
- Senaryoların süreleri (sönme, kora inme) gerçek ateş ölçüsüyle kıyaslanmadı; yalnız mertebe (3 kg odun ~1 saat).
- Izgara altı iki modelin katsayıları (alev tabakası 20 W/m²K, 1800 K; film 850-1000 K, yarı yarıya) literatür
  mertebesinden, bu sahneye özel ölçüyle değil.
- 60 kat hızda gaz gerçek zamanda, odun 60 kat: aradaki gecikmenin davranışa etkisi ölçülmedi.
- Temiz siyah ölçüyle doğrulandı, canlıda Özcan'ın gözüyle değil; "fırça izi"nin zeminin dokusu olduğu Claude'un
  referansla kıyası.
- **Çökmenin kök nedeni bulunmadı.** "an illegal memory access", her seferinde bir kütüğün OptiX yapısı kurulurken
  (`ates_renderer.cpp`, `buildGas`, `optixAccelBuild`'in ardındaki eşitleme); hemen önceki eşitleme (`setLogs`'un ilk
  satırı) temizdi, yani hata kurulumun içinde. Ağın indisleri sınırda, girdiler doğru görünüyor. compute-sanitizer bu
  sürücüde sürece bağlanmadı. Kapanışta çalışma sırasındaki kurulumlar kaldırıldı (§2); kurulum yalnız ateş yakılırken
  (açılış ve R) yapılıyor.
- `besleme`'nin bir commit'ten öbürüne tutarlı biçimde değişmesi (titretme 4/4 söndü, öncesi 3/3 sürdü) yalnız GPU
  belleğinin düzeni değişince oldu (titretmenin tek yan etkisi 64 KB'lık bir ayırma). Bir sınır dışı okuma ya da
  yazmanın fiziğe dokunduğunu düşündürüyor; bulunmadı. Kalan ateşte (`kendi`) sonuç önceki ölçümle aynı mertebede (§3).
