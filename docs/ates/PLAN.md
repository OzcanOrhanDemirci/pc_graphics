# PLAN · sahne 12 "Ateş"

> **Taslak (2026-10-28 gece): Özcan'ın onayını bekliyor.** Tek kaynak plan; durum ve ölçümler F0'da açılacak
> `docs/ates/DURUM.md`'de. Hafızada görev kartı `grafik/project_ates_gorevi.md` (Özcan'ın brief'i birebir orada); önce
> `genel/feedback_gpu_ve_grafik.md` §37-38 ve `grafik/feedback_grafik_netlik.md` okunur.

## 0 · Hedef (değişmez)

**Dar alanda, bilimsel doğrulukta ve gerçek bir ateşten ayırt edilmesi zor bir ateş: yakıtı ve havası olan, harlanan,
azalan, kora dönen, boğulup sönen, yeniden harlanan.**

Özcan: *"Ateşin burada bilimsel ve aşırı gerçekçi olması asıl olayımız olur. 3D sahnelerde ateş daha çok temsili gibi
yapılıyor. [...] 3D bir sahnede hyper gerçekçi, bilimsel doğrulukta bir ateş hatırlamıyorum."*

Bugünkü durum (çıta): film VFX'i (Houdini Pyro) gerçekçi ama kare başına dakikalar ve yakıtsız; NIST FDS bilimsel ama
görsel değil; EmberGen gerçek zamanlı ama yakıt ve oksijen modeli yok; oyunlarda döngüyle oynatılan doku. Boşluk:
**gerçek zamanlı + yakıta ve oksijene bağlı + rengi fizikten gelen ateş.**

## 1 · Kararlar (Özcan, 2026-10-28)

| Konu | Karar |
|---|---|
| Odak | **Yalnız ateş.** *"Duman bile dar alana dahil edilmez. Bütün odağı ateşe verirsin."* Kıvılcım ateşin parçası |
| Mekân | **Karanlıkta, açık havada taş ocak.** Çevre ateşe hizmet eder |
| Duman | **Gerçekçi ama bilimsel takıntısız, pişmiş:** *"Çeşitli sabit duman efektleri olur yani. Her senaryo için birkaç tane. Bu sayede gerçek zamanlı hesaplamayız."* (önerilen dikiş §3.6) |
| Hava | **Üfleme**, baktığın yöne |
| Yakıt | Gerçekten azalır; harlanmaya göre azalma hızı değişir; azaldıkça güç düşer; eklenebilir; fazlası boğar |
| Zaman | **Hızlandırma olsun** (§3.8) |
| Ses | **Yok** |
| Durumsal | **Odun türü, ıslaklık yok** |
| Kıvılcım | **Var:** *"Arada çıtırtıyla yüzlerce ateş noktacığı etrafa saçılır ya, o ateşin bir parçası ondan olmalı."* |

Brief'teki senaryolar (davranış listesi, §2'nin testleri): hava verilen yöne harlanma ve güçlenme · yakıtın gerçekten
azalması, harlandıkça hızlanması, azaldıkça gücün düşmesi · son noktada alevsiz kor · yakıt ekleme · fazla yakıtla
oksijenin kesilmesi, zayıflama ve sönme · sönmüş ateşin dibindeki kordan üfleyerek yeniden alev.

## 2 · Başarı ölçütleri ve bitti tanımı (ölçerek)

**İlke: davranış kuralla yazılmaz, fizikten çıkar.** "Fazla odunda sön" diye bir satır yok; söndüyse hava yolları
tıkandığı ve soğuk odun ısıyı çektiği için söner. Her senaryo bir test koşusudur, betiği aynı kalır.

1. **Altı senaryo kendiliğinden çıkar** (betikli koşu, `--senaryo <ad>`): tutuşturma (kibrit → çırpı → kütük, kor
   oluşana kadar bol duman) · üflemeyle harlanma (alev rüzgâr altına eğilir ve uzar, kor sarıya döner) · kendi kendine
   yanıp kora inme · aşırı beslemeyle boğulma (kor sıcaksa toparlanır, değilse söner) · kordan üfleyerek yeniden alev ·
   besleyip sürdürme. Her koşunun güç, kütle, sıcaklık eğrisi kaydedilir.
2. **Yangın biliminin ölçüleri** (aralıklar F1'de kaynaklarıyla DURUM'a yazılır):
   - alev boyu ↔ ısı salımı: Heskestad, L = 0,235 Q^(2/5) − 1,02 D (L, D m; Q kW); kamp ateşi için Q ≈ 10-30 kW,
     D ≈ 0,4 m → L ≈ 0,2-0,5 m (kestirim, F1'de ölçü kurulur);
   - kabarma (puf) frekansı: f ≈ 1,5 / √D → D = 0,4 m'de ~2,4 Hz; ölçü: sabit noktada parlaklığın frekans tayfı;
   - McCaffrey bölgeleri: sürekli alev, kesikli alev, tüy; yükseklikle sıcaklık;
   - ışınım payı ~0,3 (odun alevi);
   - kor sıcaklığı: kendi hâlinde ~600-1000 °C, üflenince üstü; renk sıcaklıktan;
   - kömürleşme hızı ~0,65 mm/dk mertebesi (Eurocode 5, yumuşak odun, standart yangın).
3. **Kör test:** gece kamp ateşi fotoğrafları ve video kareleri ile bizimkiler karışık; referanslar yalnız yerelde,
   Özcan onaylar. Videoda aynı ölçüler (alev boyu dağılımı, puf frekansı, renk histogramı) ikisinde de çıkarılır.
   **Özcan'a giden her klip bu kıyastan geçmiş olur** (rüzgârın dersi, [[gpu-ve-grafik]] §38).
4. **Görüntü gürültüsü yok:** simülasyon dondurulur, aynı an canlı ve referans çizilir (`tools/titreme.py`); ateşin
   kendi titremesi fiziktir, ölçünün dışında kalır. Taban Orman'ın DLSS değeri.
5. **Kare süresi** (GPU zaman damgası): 1440p'de hedef ≥ 40 FPS, en kötü anda ≥ 30 (bütçe F0'da ölçülür).
6. **Özcan'ın gözü:** her aşamanın sonunda klip ve exe.

**Kapsam kilidi:** ses yok · odun türü, ıslaklık yok · tek ocak · gece · rüzgâr, yağmur, su dökme yok · duman ateşin
ızgarasının üstünde pişmiş · kamerada bulanıklık ve gren yok. Kilit ancak Özcan'ın sözüyle açılır.

## 3 · Mimari

### 3.1 Üç katman ve sıralar
Her karede: **odun** (yavaş, saniyede birkaç adım yeter) → **gaz** (hızlı, karede 1-2 alt adım) → **ışınım** (alevden
oduna ısı) → **kıvılcım** → **çizim**. Gaz ve odun birbirini her adımda besler: odun gaz verir, alev odunu ısıtır.

### 3.2 Gaz (ateşin kendisi)
- GPU'da Euler ızgarası: düşük Mach akışkanı, kaldırma (sıcaklıktan yoğunluk), adveksiyon (MacCormack/BFECC),
  basınç izdüşümü (çok ızgaralı çözücü). Alanlar: hız, sıcaklık, yakıt gazı (piroliz uçucuları), oksijen, is, duman
  (pasif, §3.6).
- **İç içe ızgara:** alev dibi milimetrik dillerle çıkar; ocağın çevresinde ince (hedef 4-8 mm), üstte kaba (2-3 cm).
  Kaç milimetrenin gerçek zamanlı sığdığı F0'ın ilk ölçümü; gerçekçiliğin düğüm noktası burası.
- **Yanma:** tek adımlı tepkime, hızı karışmayla sınırlı (eddy dissipation, FDS'in mühendislik modeli) + tutuşma ve
  **sönme ölçütü** (kritik alev sıcaklığı / oksijen oranı altında tepkime durur; boğulma ve sönme buradan çıkar).
- Is oluşumu yakıtça zengin ve sıcak bölgede, yanma bölgesinde tüketilir; yanmayan uçucu ve is duman kaynağıdır.

### 3.3 Odun (yakıt)
- Her kütük yüzey yamalarına bölünür, her yamanın derinliğe doğru 1B katmanları (FDS'in katı modeli gibi): sıcaklık,
  nem (sabit başlangıç değeri, durumsal değil), ham odun, kömür, kül. Isı iletimi, kuruma (100 °C), piroliz
  (Arrhenius, ~250-500 °C; uçucular gaza), kömürün yüzeyde oksijenle yanması (**kor**, oksijeni gazdan çeker), kül
  tabakası (koru örter ve yalıtır; üfleme kaldırır).
- **Biçim:** yüzey kütle kaybıyla geri çekilir (işaretli uzaklık alanı), kömür tabakası timsah derisi gibi çatlar
  (desen kömür derinliğinden), taşıyan kesit yanınca kütük kırılır ve çöker (Yıkım'ın katı cisim çözücüsü). Kütük
  taramaları açık lisanslı kaynaktan, kabuk dokusu taramadan; çatlak ve iç odun dokusu koddan (son çare kuralı).
- Engel olarak gaz ızgarasına girer: sıkışık yığın havayı keser (boğulmanın yarısı), soğuk odun ısıyı çeker (öbür yarısı).

### 3.4 Işınım (ısı geri beslemesi)
Alevin oduna ve odunların birbirine ışıması ateşi sürdüren asıl döngü (tek kütüğün yanamaması bundandır). Isı akısı
OptiX ışınlarıyla: yüzey yamalarından ızgaradaki ışıyan ve yutan gaza (is), ayrıca yamadan yamaya.

### 3.5 Çizim
- **Alev:** belirlenimci ışın yürütme (sabit adım, rastgele örnek yok). Işıma isin sıcaklığında Planck tayfı × isin
  tayfsal yayınımı (~1/λ) → XYZ → ekran (sıcaklık → renk tablosu önceden). Dipteki mavi: tepkime hızıyla orantılı
  CH* (431 nm) ve C₂* (516 nm) ışıması, isin az olduğu yerde.
- **Kor:** kömür yüzeyi kendi sıcaklığında kara cisim (yayınım ~0,9), kül örtüsüyle karışık; üflenince parlar.
- **Isı titremesi:** havanın kırılma indisi sıcaklıktan (n − 1 ∝ 1/T), arkadaki taşlara giden ışın sıcak hava içinde
  eğilir.
- **Çevre:** taş ocak ve zemin ateşle aydınlanır; ışıyan hücreler her karede kümelenip ışık listesi olur, gölge
  ışınları OptiX'le; yüzeyler DLSS-RR'den geçer (Orman'ın hattı). Alev, duman ve kıvılcım hareket vektörü
  taşımadığı için DLSS'ten **sonra** ayrı katman.
- **Pozlama:** ateşin parlaklığı ile gecenin karanlığı arasındaki fark gerçek kameradaki gibi; göz uyumu. Ton eşleme
  gece kamp ateşi fotoğraflarına karşı ayarlanır ("turuncu leke" oyun hatasının çaresi bu).

### 3.6 Duman (pişmiş, odak dışı)
Özcan'ın önerisi + iki dikiş:
- **Uzak tüy pişmiş:** aynı gaz çözücüsü çevrimdışı, büyük alanda ve ince ızgarada koşar; senaryo başına birkaç
  döngü (tutuşma: kor oluşana kadar bol beyaz · güçlü yanma: ince, neredeyse görünmez · boğulma: birden bol · kor:
  ince tütme · sönmüş: son tütmeler). Yoğunluk saklanır, ışık değil: duman canlıda ateşin ışığıyla aydınlanır.
- **Dikiş 1, dip canlı:** ateşin ızgarasında duman zaten bir alan daha (neredeyse bedava); ızgaranın tepesinde pişmiş
  tüye devredilir. Üfleyince alevin yanındaki duman da gerçekten itilir, kopukluk görünmez.
- **Dikiş 2, seçim fizikten:** canlı gaz, yanmadan kaçan yakıtı ve isi bildiği için hangi döngünün ne kadar
  görüneceğini o seçer (senaryo betiği değil). Pişmiş tüy üfleme yönüne ucuz bir bükmeyle eğilir.
- Bellek kestirimi (ölçülmedi): kaba tüy 64×128×64, 8 bit, 10 sn döngü 60 Hz → döngü başına ~0,3 GB sıkıştırmasız;
  F6'da biçim ve çözünürlük ölçümle seçilir.

### 3.7 Kıvılcım
- **Tetikler:** çıtırtı olayları (piroliz şiddetiyle orantılı, tohumlu rastgele), kütük çökmesi, kor üstüne kütük
  bırakmak (en bol saçılan an), üfleme.
- **Fizik:** her kıvılcım yanan bir kömür parçası (çap ~0,2-1 mm): gazın hız alanıyla taşınır, sürüklenme ve yerçekimi;
  yandıkça küçülür, ışıma ve taşınımla soğur; rengi kendi sıcaklığından; sönünce kaybolur, taşa düşerse bir an parlar.
- **Çizim:** DLSS'ten sonra; bir kare süresince aldığı yol kadar kısa bir iz (hareket eden noktanın kare içindeki
  doğru örneklemesi; yoksa nokta karede sıçrar). Bu görüntü bulanıklığı değil, Özcan'ın gözüyle karara bağlanır.

### 3.8 Zaman hızlandırma
Yalnız **yavaş süreç** hızlanır: odunun kütle kaybı, kuruma, kömürleşme, kül (×1 … ×60). Alev her zaman gerçek
hızında ve odunun o anki hâlinin verdiği gerçek güçte kalır (gaz akısı hızlandırılmaz; yoksa ×60'ta alev 60 kat
büyürdü). Yani hızlandırılan, yakıtın hâlinin zaman atlaması.

### 3.9 Etkileşim
- **Yürüyüş:** ocağın çevresinde birkaç metre (WASD, çömelme); alan dar.
- **Tutuşturma:** kibrit, baktığın noktaya.
- **Üfleme:** tuşu basılı tuttukça baktığın yöne; jet ~5-10 m/s ve oksijenli hava, uzaklıkla zayıflar.
- **Yakıt:** ocağın yanındaki odun yığınından iki boy (çalı-çırpı, kütük; tür değil boy): baktığın yere bırakılır, katı
  cisim çözücüsü yerleştirir.
- **Panel:** zaman hızı, güç (kW), kütle, en sıcak kor, oksijen; ölçüm için.

### 3.10 Yeniden kullanılan
OptiX hattı ve DLSS-RR (Orman) · göz uyumu ve panel · `tools/titreme.py`, `kararlilik.py` · Yıkım'ın katı cisim
çözücüsü · Orman'ın varlık yükleyicisi (Poly Haven `stone_fire_pit`, CC0).

## 4 · Aşamalar (her biri ölçü + Özcan'a klip ve exe ile biter)

| | Ne | Çıktı |
|---|---|---|
| F0 | İskelet (gece, taş ocak, zemin) + **bütçe ölçümü** (ızgara çözünürlüğü × adım süresi, alev ışın yürütme maliyeti) + referans toplama + ölçü araçları | bütçe tablosu, çözünürlük kararı |
| F1 | **Brülör alevi:** sabit gaz kaynağı, yanma, is, Planck ışıması, mavi dip, ton eşleme | Heskestad, puf, kör test (alev) |
| F2 | Çevre ışığı ve gölgeler, ısı titremesi | gürültü ölçümü |
| F3 | **Odun:** katmanlı ısıl model, piroliz, kömürleşme, çatlama, ışınım döngüsü; tutuşturma | senaryo 1, 3 |
| F4 | Kor, boğulma, sönme, yeniden harlanma; üfleme; yakıt ekleme, çökme | senaryo 2, 4, 5, 6 |
| F5 | Kıvılcım | sayım, ömür, Özcan'ın gözü |
| F6 | Pişmiş duman ve dikişler, zaman hızlandırma | bellek, görünüm |
| F7 | Cila ve kapanış: §2'nin hepsi | bitti |

## 5 · Riskler

- **Alevin ince yapısı:** gerçek zamanlı ızgara milimetreye inemezse alev "yumuşak" kalır. F0 ölçer; yetmezse alt ızgara
  ayrıntısı (fizikten türetilmiş, yalnız çizimde) seçeneği DURUM'a gerekçesiyle.
- **En ağır sahne:** simülasyon + hacim çizimi aynı karede; FPS hedefi F0'dan sonra dürüstçe konur.
- **Hareketi göremem:** her ölçü videodan da çıkarılır, son söz Özcan'ın gözü.
- **"Bilimsel"in sınırı:** yangın mühendisliğinin doğrulanmış modelleri, tam kimya değil.
- **DLSS ve hacim:** alev DLSS dışı katmanda; çözünürlüğü ve kenarı F1'de ölçülür.

## 6 · Açık (Özcan'ın kararı)

Önerilenler, onay bekliyor: dumanın iki dikişi (§3.6) · kibritle tutuşturma ve iki boy yakıt (§3.9) · kıvılcımın kısa
izi (§3.7) · gökyüzü: aysız ama tam siyah değil (ufukta hafif gök ışığı; duman tepede seçilir) · FPS hedefi ≥ 40 ·
ad **Ateş** (kimlik `ates`, galeride 12) · isteğe bağlı: maşayla karıştırma (kıvılcımın doğal tetikleyicisi), üflemede
nefes süresi.
