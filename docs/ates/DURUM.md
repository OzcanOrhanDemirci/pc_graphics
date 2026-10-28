# DURUM · sahne 12 "Ateş"

> Plan `docs/ates/PLAN.md` (hedef, kararlar, mimari); varlıkların kaynağı ve lisansı `docs/ates/VARLIK.md`. Hafızada
> görev kartı `grafik/project_ates_gorevi.md` (Özcan'ın brief'i birebir). Bu dosya: nerede olduğumuz, ne ölçüldü, ne
> açık. Sabitlerin kaynağı başlıklarda, her sabitin yanında (`ates_gas.h`, `ates_wood.h`, `ates_sparks.h`).

## Özcan: nasıl açılır, ne denenir

- Varlıklar git'te değil: önce `python assets/ates/fetch.py` (beş CC0 varlık, ~70 MB). DLSS için `third_party/fetch.sh`.
- `build\pc_graphics.exe` → galeride **0**, sonra **iki kez aşağı ok** (12. sahne) → Enter. Fare bakar (Tab imleci bırakır),
  **W A S D** yürür, **C** çömelir. **Sol tık (basılı)** üfler: ağız baktığın şeyin 30 cm önünde. **F** kibrit çakar,
  **Q** bir avuç çalı, **E** kütük atar. **T** zamanı hızlandırır (×1, 5, 15, 30, 60; yalnız odunun zamanı, alev
  hep gerçek hızında). **R** ateşi baştan yakar, **1-4** bakışlar, **↑ ↓** pozlama.
- Komut satırı: `build\pc_graphics.exe --scene ates --view 3 --sure 10 --out <yol>` (resim), `--bench 240 --canli`
  (kare süresi), `--senaryo <ad>` (§3; eğri `<yol>.csv`, resimler `<yol>_<t>s.png`), `--set debug.wood=1` (kütük
  kütük ısı dengesi).

## 1 · Şu an (2026-10-28 sabah)

Plan onaylandı (Özcan, 10-28: *"sahneyi tamamlayana kadar çalışmaya devam et [...] tam yetkilisin"*). F0-F5'in kodu
var: gaz, odun, kor, kıvılcım, ışık, etkileşim, zaman hızlandırma. F6'dan canlı dumanın çizimi var ama kapalı (§5),
pişmiş uzak tüy yok; F7 (kapanış ölçüleri, kör test) yok.

**Bu sabahın asıl bulgusu: ateş kendi kendini sürdürmüyordu, sürdürüyor görünmesi bir hataydı.** Gaz ızgarasında bir
kütüğün içine düşen hücre, o anki gazını (yanan yüzün dibinde, uçucuca zengin) donmuş hâlde tutuyordu; komşuları her
adımda ondan örnekliyor, yakıt hiç tükenmiyordu. Ölçü: 60 kat hızda 0,4 g/s yakıttan 40 kW ısı (yakıtın verebileceği
~6 kW). Hata kapatılınca (`kGhost` + duvarda örnekleyenin kendi değeri) ateş gerçek zamanda iki dakikada söndü.
Sönmenin sebepleri tek tek ölçülüp fizikle kapatıldı (§2). Bugünkü hâliyle ateş gerçek zamanda ~15 kW'ta yanmayı
sürdürüyor, 60 kat hızda ~40 dakikada (odun zamanı) yanıp sönüyor; senaryoların ikisi tam, üçü yarım, ikisi yürümüyor
(§3).

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
| Duman hiç azalmıyordu: 10 sn'de 0,39 g, bir hücrede 104 g/m³ (soğuk yüzlerin dibinde sis) | Yüz dibinde yoğuşmaz; sıcak gaza dönen damlacık buharlaşıp yanar; yüze çöker (1 cm/s): 0,04 g, en yoğun 24 g/m³ |

**Açık ölçü:** gerçek zamanda ısı salımı, odunun verdiği yakıtın (uçucu + CO) ~1,4 katı; 60 kat hızda ~1. Brülör
testinde %90 (doğru). Kaynağı bulunmadı (§5).

## 3 · Senaryolar (`--senaryo <ad>`, PLAN §2.1)

El ocağın açık yanından (kütükler x boyunca), kömürlere bakarak çömelmiş; kibrit ve odun bakışın yığına değdiği yere,
üfleme bakış boyunca. Resimler aynı gözden.

Ölçüm 2026-10-28 sabah, son fizikle (`4a016ba`). HRR: gazın ısı salımı; "kor": 800 K üstü yüzey.

| Senaryo | Beklenen (brief) | Çıkan | Hüküm |
|---|---|---|---|
| `kendi` (gerçek zaman, 3 dk) | yanmayı sürdürür | 41 → 14,7 kW, 140. sn'den sonra düz; piroliz 0,72'ye inip 0,84 g/s'ye döner; kor 2700 cm² | ✅ |
| `ufleme` (15 sn üfleme) | rüzgâr yönüne harlanır, güçlenir | kor yanması %31 artar (0,181 → 0,239 g/s), en sıcak yüzey +29 K; alevin HRR'si düşüşünü sürdürür (36 → 29 kW), üfleme bitince 1 kW sıçrar | ◐ kor harlanıyor, alev güçlenmiyor |
| `kor` (odun 60 kat) | yanar, azalır, son noktada alevsiz kor | ~40 dk yanar (10 → 2 kW), 50. dk'da alev ve kor birlikte biter; 1,69 kg odun kalır | ◐ alevsiz kor evresi yok |
| `bogulma` (6 kütük, 8 kg) | oksijeni kesilir, zayıflar; kor sıcaksa toparlanır | atıldığı an 36 → 30 kW (%16), 2 sn'de toparlanır; 3. dk'da yeni kütükler tutuşup 47 kW | ◐ zayıflama hafif, sönme yok |
| `besleme` (5 dk'da bir kütük) | beslendikçe sürer | yarım saat 11-27 kW, son iki kütükle 47-52 kW | ✅ |
| `yeniden` (35 dk sonra 15 sn üfleme) | kordan yeniden alev | kor 1490 K'e çıkar, kor yanması %22 artar; alev dönmez (0,2-0,6 kW) | ❌ |
| `tutusturma` (kibrit) | kibrit → çıra → çırpı → kütük | çıra 1350 K, 0,2 kW'lık parlama; kibrit bitince söner | ❌ |

## 4 · Ölçümler

| Ne | Değer | Nereden |
|---|---|---|
| Alev boyu (brülör, 17 kW) | %95 ısı salım yüksekliği 0,33 m; Heskestad 0,32 m | `c6a923d`, `--gaz` |
| Işınım payı (brülör) | 0,22-0,25 | `--gaz 1200`: 20 kW'ta HRR 18,0, ışınım 4,4 kW |
| Kare, bakış 0 (1440p, DLSS-RR DLAA, canlı) | 38,7 FPS, 25,9 ms (en kötü 29,4); gaz 15,0 (3 adım × 5,0), DLSS 5,9, ateş 1,3, odun 0,9 ms | `--bench 240 --canli`, 10-28 sabah |
| Kare, bakış 3 (közün içine) | 33,6 FPS, 29,7 ms; ateşin geçişi 6,1 ms; gaz gerçek zamanın %84'ü | aynı |
| Gaz adımı | 4,6-5,0 ms (128×192×128, 7,5 mm) | `--gaz`, `--bench` |
| Kıvılcım | 10 sn'de ~630 doğan; adım + çizim 0,2 ms | §2 |

## 5 · Açık işler (öncelik sırasıyla)

0. **Senaryoların üçü yarım, ikisi yürümüyor** (§3): alevsiz kor evresi, boğulmanın derinliği, kordan yeniden alev,
   tutuşturma. Hepsinin ortak noktası ızgaradan ince alevler: kömürün ve talaşın alevi, korun üstündeki uçucunun
   tutuşması.
1. **Tutuşturma yürümüyor:** kibrit talaşı 1350 K'e ısıtıp 0,2 kW'lık bir parlama veriyor, kibrit bitince sönüyor.
   Talaşın ve çırpının alevleri hücreden (7,5 mm) ince: ızgara onları taşıyamıyor. Aday: ince yakıtın uçucuları
   laminer tabakasız karışsın (`gas.wallMix=0` denemesi parlamayı 0,27 kW'a çıkardı, yetmedi) ya da ince yakıtın
   alevi CO filmi gibi ızgara altı yansın.
2. **Isı salımı / besleme 1,4** (§2): katı köşe düzeltmesi oranı değiştirmedi; aday: yakıtın enjekte edildiği
   hücrelerin genleşmesi ile yarı Lagrange adveksiyonunun korunumsuzluğu. Gazın yakıt defterini (girdi, yanan,
   ızgarada kalan, çıkan) kütükle koşuda tut.
3. **Duman:** çizimi var (`smoke.ext=4.4`), varsayılan kapalı. Alevin üstünde ateşin ışığıyla turuncu bir iz
   güzel; ama ateşin dibinde kalan duman alevin binlerce lüksünde kütükleri örten parlak bir perdeye dönüyor
   (en yoğun 24 g/m³; gerçek kamp ateşinin dibinde duman yok denecek kadar az). Aday: dumanın ışığına gölge
   (kütükler ve dumanın kendisi), dipte yoğuşmanın kaynağı (soğuk yüzlerden kaçan uçucu). Uzak tüy (pişmiş) yok.
4. **FPS ≥ 40** (Özcan kuralı: DLSS en az Kalite, çözünürlük düşmez): bakış 0'da 38,7, bakış 3'te 33,6. En büyük
   kalem gaz (karede 3 adım, 15 ms); DLSS-RR DLAA 5,9 ms (Kalite kipine inmek Özcan'ın izin verdiği bir seçenek).
5. **Kor görünümü:** kömürler hep aynı kırmızı, külde çeşit az; mavi CO alevi büyük (önceki oturumun notu; bu
   sabahki fizik değişikliklerinden sonra yeniden bakılmalı).
6. Kör test (gerçek gece kamp ateşi fotoğraf ve videolarıyla; referanslar Özcan'da), puf frekansı ölçüsü,
   `tools/titreme.py` ile görüntü gürültüsü.
7. Galeri resmi (`assets/gallery/ates.jpg` yok) ve README bölümü.

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
