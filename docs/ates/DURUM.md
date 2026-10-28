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

Yakıt defteri (`2fb24fb`) gelince ateşin gücü dürüstleşti: kendi hâlinde gerçek zamanda ~7,6 kW'ta duruyor (önce 14,7;
fazlası taşımanın yarattığı yakıttı). Brülör testinin eski "%90"ı tesadüftü: brülörün 450 K'lik yakıtı katran olarak
yoğuşup kayboluyor, taşımanın fazlasını örtüyordu.

## 3 · Senaryolar (`--senaryo <ad>`, PLAN §2.1)

El ocağın açık yanından (kütükler x boyunca), kömürlere bakarak çömelmiş; kibrit ve odun bakışın yığına değdiği yere,
üfleme bakış boyunca. Resimler aynı gözden.

Ölçüm 2026-10-28 öğlen, yakıt defteriyle (`2fb24fb`). HRR: gazın ısı salımı; "kor": 800 K üstü yüzey.

| Senaryo | Beklenen (brief) | Çıkan | Hüküm |
|---|---|---|---|
| `kendi` (gerçek zaman, 3 dk) | yanmayı sürdürür | 22 → 7,6 kW, 2. dakikadan sonra düz; piroliz 0,59'a inip 0,70 g/s'ye döner; kor ~2450 cm² | ✅ |
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

## 5 · Açık işler (öncelik sırasıyla)

0. **Senaryoların ikisi yarım, ikisi yürümüyor** (§3): alevsiz kor evresi, üflemeyle alevin güçlenmesi, kordan
   yeniden alev, tutuşturma. Ortak noktaları ızgaradan ince alevler: talaşın alevi, korun üstündeki uçucunun
   tutuşması.
1. **Tutuşturma yürümüyor:** kibrit talaşı 1350 K'e ısıtıp 0,2 kW'lık bir parlama veriyor, kibrit bitince sönüyor.
   Talaşın ve çırpının alevleri hücreden (7,5 mm) ince: ızgara onları taşıyamıyor. Aday: ince yakıtın uçucuları
   laminer tabakasız karışsın (`gas.wallMix=0` denemesi parlamayı 0,27 kW'a çıkardı, yetmedi) ya da ince yakıtın
   alevi CO filmi gibi ızgara altı yansın.
2. **Alevin ekseni çok sıcak, puf yavaş** (§4): sürekli alevde 1550 K (McCaffrey ~1100), puf 1,3 Hz (beklenen 2,4).
   Aday: 7,5 mm'de alev kenarındaki karışmanın (çekilen hava) eksikliği; Smagorinsky katsayısı ve vortisite
   hapsi ölçülerek.
3. **Duman:** çizimi var (`smoke.ext=4.4`), varsayılan kapalı. Alevin üstünde ateşin ışığıyla turuncu bir iz
   güzel; ama ateşin dibinde kalan duman alevin binlerce lüksünde kütükleri örten parlak bir perdeye dönüyor
   (en yoğun 24 g/m³; gerçek kamp ateşinin dibinde duman yok denecek kadar az). Aday: dumanın ışığına gölge
   (kütükler ve dumanın kendisi), dipte yoğuşmanın kaynağı (soğuk yüzlerden kaçan uçucu). Uzak tüy (pişmiş) yok.
4. **FPS ≥ 40** (Özcan kuralı: DLSS en az Kalite, çözünürlük düşmez): bakış 0'da 38,1, bakış 3'te 33,7. En büyük
   kalem gaz (karede 3 adım, 15 ms). İki ölçülmüş kaldıraç, ikisi de Özcan'ın kararı: (a) basınç çözücüsünde adım
   başına 1 V-döngüsü (`gas.vcycles=1`): adım 4,91 → 4,40 ms (karede ~1,7 ms), artık 4e-3 → 5e-2, brülör ölçüleri
   aynı (HRR 19,6 kW, %95 yüksekliği 0,40 m); (b) DLSS-RR'yi DLAA'dan Kalite kipine almak (ölçülmedi).
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
