# DURUM · sahne 5 "Koy" (su altı)

> Tek kaynak durum panosu. Son plan: **`docs/koy/PLAN_ADA.md`** (üçüncü bölüm "Ada", 🏁 bitti); önceki planlar
> `PLAN.md`, `PLAN_AMFORA.md`. Veri: `docs/koy/VERI.md`. CC0 taramalar: `assets/koy/fetch.py`, `assets/koy/LICENSES.md`.

## Şu an · 2026-10-04 akşamı · Özcan canlı baktı: iskele salonunun iki revizesi

**Özcan (canlı monitörde, batığa gelmeden):** *"İskele güzel olmuş 2 revize lazım: 1) iskelenin altının başladığı yerde bir
ışık duvarı var, doğal değil yani görünmez duvar gibi saydam duvar [...] rtxe en yakışmayan şey. [...] en sığ yerlerde
delikli ve gölgeli çok iyi ama orta noktasında bir alan simsiyah ve çok yapay duruyor. Karanlık ayrı simsiyah ayrı. [...]
Yine o gerçekçi rtx fizik ne derse o ışığımıza hiç uymuyor."* Titreme, netlik ve FPS sorunu yok dedi.

**Teşhis:** suyun dağınık ışığını yapının altında "yüzeyden yukarısı ne kadar açık" (45°'lik koni) ile kısıyordum. (1) Bakış
ışını boyunca yalnız 3 noktada örneklenince başlığın kenarı bir örneği geçtiği anda ışının bütün bir dilimi birden
kararıyordu: ekranda saydam bir duvar. (2) Koni yanlardan gelen ışığı (açık su, dışarıda güneşteki kum) hiç görmüyordu.

**Yapılan:**
- **Işık hacmi** (`__raygen__bakeHall`, `hall.box/cell/dirs/bounces`): iskelenin altında 25 cm'lik hücrelerde (112×19×164)
  her yöne 256 ışın: açık suya kaçan suyun o yöndeki ışığını (Snell penceresinden gök, havada açık mı diye sınanarak; dışında
  saçılmış ışık), yüzeye çarpan o yüzeyin ışığını (güneş haritasından güneş, kum ya da beton yansıtması), aradaki su ile
  getirir; açık sudaki aynı toplama bölünür. İki sekme turu (kum↔tavan). Yükleme 0,13 sn.
- **Bakış ışınının payı sütun yürüyüşünde:** güneş sütunlarını hesaplayan geçiş her ışını ince adımlarla zaten yürüyor;
  dağınık ışığın payı da orada toplanıp sütun hacminin boş dördüncü kanalına yazılıyor, örnek aynı okumada alıyor (duvar
  yok; 8 noktalı ilk deneme +20 ms idi, bu yol ~+0,5 ms).
- **Fizik "simsiyah" dedi:** başlık hem alçak hem geniş (22×14 m); ortaya yalnız yere neredeyse paralel ışınlar ulaşıyor,
  onları da ayaklar kesiyor: açık suyun %0,1-1'i. Özcan'ın ilk izniyle (*"iskeleyi altı karanlık kalacak şekilde
  konumlandırıp şekillendirebilirsin"*) yapı değişti: **8 kırık delik** (4 idi, ikisi aynı bölmedeydi), her biri ayrı bölmede,
  birbirinden en uzak bölmelere; içeriye güneş lekeleri ve sütunlar düşüyor. Filizler yarıya (her ~40 cm).
- **Göz:** gerçek bir gözün bu karanlığa uyumu ve parlak açıklıkların göz merceğinde yaydığı **perde** (veiling glare):
  `display.glare = 0.06` (karenin ortalama ışığının %6'sı her piksele: yalnız karşıtlığı yüksek karelerde, salonda fark
  eder) ve `display.adapt` 0,3 → 0,5. Etkisi: batıkta +0,17 EV, 43 m'de +0,5 EV (derin hâlâ koyu lacivert).
- **Ölçüler:** testler 13/13; pencere testinde 30-32 fps (önce 33-35; kenar pikselleri başlangıçta 477 bin → 539 bin: yeni
  ışık lekeleri ve artık seçilen karanlık ayrıntı). Beton ayrıntı katmanının bedeli ölçülemeyecek kadar az
  (`pier.concrete_detail` ile kapanır).

**NE DOĞRULANMADI:** canlı pencerede bakılmadı (Özcan bakacak); salonun kıpırtısı yeniden ölçülmedi (ışık hacmi yavaş
değişen bir alan, sütun hacmiyle aynı okumada); batığın içi aynı yöntemle pişirilmedi (orada eski koni duruyor).

## 🏁 Ada bitti (B0-B6), Özcan'ın bakacağı yer · 2026-10-04 akşamı

> **Son değişiklik (Özcan, 10-04 akşamı, karelere telefondan baktı):** *"Çayır yani o hareketli yosunlara gerek yok. Bu
> sahnede amacımız fizik değil. Her sahnenin bir amacı vardı. Bu sahnede gerçekçi su ışık ve yüksek çözünürlük yani net
> ve stabil görüntü hedefledik. Yosun fiziği bu sahnenin aslında amacına ters. Ondan kaldırabilirsin."* → Deniz çayırı
> sahneden ve koddan kaldırıldı: yapraklar ve salınım çekirdeği, her karedeki güncelleme, çizicide şerit eğrisi desteği
> (OptiX eğri ilkeli, ikinci isabet kaydı, güncellenen eğri ağacı). Kod git geçmişinde (commit `a667573` ve öncesi).
> Malzeme 2 numara boş yer tutucu (arkasındakilerin numarası değişmesin). **Yan kazanç:** kare süreleri çayırın hiç
> görünmediği yerlerde bile düştü: açık sığ kum 32 → 27 ms, eski çayırın önü 39 → 27,5 ms ve 46 → 31 ms; otomatik test
> 33,3-35,4 fps (önce 32,3-34,5). Nedeni ayrıştırılmadı (boru hattındaki eğri bayrağı, eğri ağacı ya da her karedeki
> yeniden uydurma). Testler 13/13. Aşağıdaki çayır satırları o anki durumdur.
>
> **Dokular en az 8K (Özcan, aynı akşam):** *"gözle fark edilmesinden ziyade tüm assetler 8K olmuş olsun [...] çok az
> bile olsa o netlik ve keskinlik fark ediyor."* İskele ve batığın ahşabı (Poly Haven rough_wood) ve iskelenin betonu
> (concrete_wall_008) 4K → 8K (`ahsap_8k`, `beton_8k`; md5 Poly Haven'a karşı, sha256 `fetch.py`'de; 8K yoksa 4K'ya
> düşer). Ölçü: 1 m'deki ayakta ve batık güvertesinde 4K ile 8K arasındaki ortalama fark 0,1 / 255 (hesap: ahşap 21 cm'den,
> beton 1,1 m'den uzakta 4K ekranın ötesinde). Bedeli: kartta 12,6 → 13,6 / 16 GB, dokular 9,1 → 10,1 GB. Kare süresi
> değişmedi. Taranmış kaya (boulder_01) Poly Haven'da 8K'da bitiyor, 16K'sı yok (Ultra'da üstüne 16K kaya ayrıntı katmanı
> biniyor). Şu an Ultra'da: kum 16K, kaya ayrıntısı 16K, taranmış kaya, amfora kili, kabuk, ahşap, beton 8K.
>
> **16K ayrıntı katmanı ("atlas") uyan her malzemeye (Özcan, aynı akşam):** iskelenin betonu (ıslak ve kuru) kendi 8K
> taramasını üçte bir karoyla ikinci kez alıyor (0,9 m: 0,11 mm, 16K kaya katmanının yoğunluğu; ek bellek yok); deniz
> taşları ve batığın demir kabuğu 16K kaya katmanını alıyor (Yüksek'te 8K, Standart'ta yok). Ölçü (aynı an, katman
> ağırlığı 0 ile karşılaştırma, en çok değişen 640×400 kutu): taşlar ortalama 0,28 / 255 (en çok 26: pürüzsüz çakıl
> taş dokusu kazandı, gözle açık), beton 0,44 (en çok 12), demir 0,13 (en çok 15). Kare süresi değişmedi (±0,6 ms).
> **Yapılamayanlar:** halat ve midye için Poly Haven'da hiç tarama yok (ne kabuk ne halat); yeni bir 16K set (renk +
> normal, mip'li, sıkıştırmasız) ~2,9 GB ve kart 13,6 / 16 GB: sığmıyor. Yol: (1) Ultra'da kullanılmayan 8K setleri
> (kum_8k, kaya_ayrinti_8k) yüklememek ~1,7 GB açar ama paneldeki geçiş anlık olmaktan çıkar; (2) blok sıkıştırma (BC7
> renk, BC5 normal) bütün dokuları ~dörtte birine indirir: büyük iş, netliğe etkisi ölçülmeli; (3) halat ve midyeye
> taramasız, hesapla üretilen ince ayrıntı (lif, büyüme çizgileri): çözünürlük sınırı yok, sanat işi.

### Özcan'ın bakacağı yer

**Açmak:** `build\pc_graphics.exe` → galeride **05 Ada**. Yükleme ~30 sn (16K kum ve kaya taramaları, adanın ve
kayaların ağları, batık). Dalgıç beton iskele başlığının altında, merdivenin dibinde, açığa bakarak başlar.

**Tuşlar:** **W A S D** yüz · **Boşluk** yukarı · **C** aşağı · **Shift** güçlü vuruş · **Tab** fareyle bakış (ya da
sağ tık basılı) · **F** fener · **P** zamanı durdur · **K** kostik · **E** kenar pikselleri · **↑/↓** pozlama · **F5**
ayar dosyasını yeniden okur. Panelde derinlik, basınç, ppO2.

**Sırayla dene:**
1. **Salon (başlangıç, 1-3 m):** etrafına bak; kırık deliklerin altına git, kumdaki ışığın dalgayla salınışını izle.
   Kıyıya dön (+z): tahta yürüyüş yolunun altı, tahta aralıklarından kuma düşen kıvrık ışık çizgileri.
2. **Sığ kum (4-9 m):** dubaların arasından açığa yüz: kostik ağı, deniz yıldızları, taşlar.
3. **Amforalar (~12 m, z ≈ −52):** kayalara yaslanmış yığın, çevresinde tek tük amforalar. Yakında **F**: pişmiş toprak.
4. **Batık (~22 m, z ≈ −97):** kıç kasarası sisten çıkar; sancak tarafındaki çöküntüden içeri gir, **F** ile ambar,
   toplar, postalar; küpeştede yelpaze mercanları (fenerde kızıl).
5. **Şamandıra (z ≈ −160):** midyeli halatı izleyerek in; 35-40 m'de dip belirir. 66 m'yi geçersen (ppO2 1,6 bar) görüş
   daralır, kararır, merdivende yeniden başlarsın (~7 sn).

### Yapılanlar (B5-B6; B1-B4 aşağıda)

- **B5 · batık:** 38 m'lik üç direkli bir kalyon, tek bir gövde fonksiyonundan (dip açısı, içe çekik bordalar, güverte
  çizgisi, ayna kıç) kereste kereste: 76 posta, 3277 kaplama parçası (1207'si eksik: gürültüyle seçilen bölgeler çöker,
  tek tek rastgele eksik dama tahtası gibi okunuyordu), omurga, baş ve kıç bodoslaması, ambar ağızlı iki güverte, kıç
  ve baş kasaraları, pencereli ayna ve kıç balkonları, lumbaralar (birkaç kapağı sarkık), 13 top, kırık direkler (ana
  direğin üstü ve seren kumda), cıvadra, dümen, çapa, safra taşları. Yamaçta başı aşağıda (yamacın eğimi), sancağa 18°
  yatmış, sancak tarafı çökmüş (kaburgalar açıkta), 1,3 m gömülü. Ahşap gri kahve, kabuk yok (girinti kuralı bütün
  ambarı girinti sayıp lekeliyordu). 57 yelpaze mercanı batığın verdiği noktalarda (küpeşte, kasara kenarı, kaburga
  tepesi; yüzeye serpince gövdenin içine düşüyorlardı), 0,7-1,3 m, yedi kuşak dallanma.
- **B6 · sanat turu:** AgX "punchy" görünüm (üs 1,35, doygunluk 1,3: 10 m'de her şeyi saran düz camgöbeğini kırdı);
  su biraz berrak (parçacık 0,55: IA ile IB arası, uzak siluet ve karşıtlık için; Akdeniz açığı için gerçekçi); göz
  uyumu açık su ölçüsüyle %30 (salon karanlık kalır), rengin %50'si; deniz çayırının kenarı 1,6 m boyunca seyrelir ve
  kısalır (uzaktan budanmış çit duvarı gibiydi). Galeri kartı "05 Ada", galeri
  resmi salon (256 örnek), README EN/TR sahne 5 bölümü ve üç görsel, GOSTERI/DEMO tuşları, oksijen testi.
- **Hız kararları:** kenar örneği 16 → 12 (fark gözle seçilmedi, salon 44 → 33 ms), kostik dilimi 32 → 20 (8,4 → 5,2
  ms; 30 m'de zaten sönüyor), dalga salınımı en çok 4 m, gök görünürlüğü 3 örnek, havadaki ışın en çok 400 m.

### Ölçüler

- **Kararlılık** (40 kare yüzüş, 256 örnekli referansa karşı, `--kararlilik` + `tools/kararlilik.py`):
  salona yüzüş piksel 0,0023 (p99 0,038), blok 0,00057, bağıl 0,25, FLIP 0,0116; batığa yaklaşış piksel 0,0007, blok
  0,00016, bağıl 0,22, FLIP 0,0084. Çayıra yüzüş (ince yapılar): piksel 0,0014 (p99 0,013), blok 0,00027, bağıl 0,17, FLIP 0,0090; batık yolu yelpazeler büyüdükten sonra piksel 0,00076, FLIP 0,0089 (değişmedi). Fener'in TAA tabanı piksel 0,0053-0,0061, FLIP 0,026-0,038: üçü de yarısının altında.
- **Kare süresi** (2560×1440, komut satırı): salon 33 ms, açık kum ve koridor 20-27 ms, sığ kumda 1 m yükseklikte
  32-39 ms, batık 19-21 ms, çayırın içine bakınca 43-53 ms. Uygulamada otomatik test (`--autotest-only koy`): başlangıç 32,8 fps, yüzerken 32,3, doku ve dalgacık seçenekleri 33,8-34,5, 32 kenar örneğiyle 26,0; sahne kapanınca kart boşalıyor (14,9 / 16 GB boş).
- **Bellek:** kartta 12,6 / 16 GB (dokular 9,1 GB; 8K ahşap ve betonla 13,6 ve 10,1). **Testler:** 13/13 (Snell penceresi 48,41°, kostik enerjisi, dalga
  defteri, ışık alanının Kd'si, ppO2 1,600 bar 65,9 m'de, 50 m'de doz yok, 70 m'de tünel 2,2 sn → kararma 6,7 sn →
  merdiven 7,2 sn).

### NE DOĞRULANMADI

- **Özcan'ın gözü:** canlı pencerede yüzülmedi; her şey komut satırı kareleri, kararlılık yolları ve otomatik testin
  ekran görüntüleri. Salonun "tedirgin edici" olup olmadığı, ışığın dalgayla salınışının canlı hissi kareden tartıldı.
- **Bağımsız gözden geçirme yok:** Özcan ajan istemedi; kodu ve belgeleri yalnız ben okudum. Kendi gözden geçirmem:
  oksijen ve yeniden başlama akışı, dubaların ekran kartı tamponları, güneş haritalarının ömrü (serbest bırakılmıyor
  sandım: uygulama sahne kapanınca kartı sıfırlıyor, sızıntı yok); otomatik testin "kenar 32" adımı seçenek listesine
  12 eklenince 16'yı seçiyordu, düzeltildi.
- **Batığın içi:** ışık ve fener yalnız durağan karelerle; içeride yüzerken çarpışma (dalgıç kerestelerin arasından
  geçebiliyor mu, takılıyor mu) denenmedi.
- **Hareket:** dubaların dalgayla inip kalkışı canlı görülmedi (kod ve tek kareler).
- **Kararlılık** yelpaze mercanlarının dibinde (yakın planda ince dallar) ölçülmedi; salon ve batık yolu ölçüldü
  (çayır kaldırıldıktan sonra yeniden ölçülmedi: yalnız bir öğe eksildi).
- ~~Kare hızı çayırın içinde 30'un altında~~: çayır kaldırıldı (yukarıda); en yavaş yer artık başlangıç salonu, 32 ms.
- **Yükleme** ~30 sn; kısaltılmadı.

**hafıza itildi:** evet (Ada görevi 🏁, hasat [[gpu-ve-grafik]] §24).

## B1-B4 · 2026-10-04 öğleden sonra · büyük dünya, ada ve iskele, dubalar, süsler

**Yapılanlar (her biri kareyle bakılarak):**
- **B1 · büyük dünya:** adanın çevresinde yavaş yavaş derinleşen kum (iskelenin ucu 3,5 m, amforalar ~12 m, batık
  ~22 m, şamandıra ~47 m, 66 m z ≈ −202), ışık alanı 120 m'ye ve 7 dip derinliğine, kostik 30 m'ye (ötesinde söner),
  analitik kum dalgacıkları (kıyıya paralel, derinde söner; 🪤 hızlı `cosf` binlerce radyanda bozuluyordu: faz önce tam
  turlara indirildi), basınç ve oksijen zehirlenmesi (ppO₂ 1,6 bar üstünde doz, tünel görüş, kararma, merdivende
  yeniden başlama), `--map` (yerleşim haritası + profil), `--kareler` (tek yüklemede çok bakış; `--yerden` ile kumdan
  yükseklik).
- **B2 · ada ve iskele:** ada bir uzaklık alanı (alçak falez, tepe, su altında kaya, köşeli kaya blokları), koyun iki
  yanından iskele boyunca uzanan iki sualtı kayalık sırtı (başlangıçta çevre kaya: kemer dersi). İskele: kumsaldan tahta
  yol (tahtalar boyuna, aralıklı: güneş azimutu iskele boyunca, ışık çizgileri) ve ucunda **alçak beton T başlığı**
  (22 × 14 m, kirişli tavan, 35 kabuklu ayak, 4 kırık delik ve paslı filizler, altında moloz). **Özcan görev sırasında
  bir fotoğraf ve iki mesaj gönderdi:** iskele altı çevresinden karanlık, aralıklardan ışık sızıyor; *"gerçek karanlık
  [...] biraz korkutucu olsun [...] altına girmek tedirgin etmeli"*. Karşılığı: beton başlık, gök görünürlüğü haritası
  (güvertenin altında suyun dağınık ışığı da söner), göz uyumu %30 (açık su ölçüsüyle), başlangıç başlığın altında.
  Su üstü artık Snell penceresinden izleniyor (güverte, ayaklar, ada); gölge ışını havada sürüyor; güneş haritası
  (derinlik + havadan geçiş + gök) ışık sütunlarına ve yüzeylere; gölgeler dalgayla salınıyor. Gözün renk uyumu %50
  (kırmızı gamın dışına düşünce kazanç sınırlı: 10 m altında beyaz kare hatası buradan çıktı, düzeltildi).
- **B3 · dubalar ve halat:** iki yanda ve uçta duba hatları (163 duba dalgayla iner kalkar: dalga yüksekliği her karede
  ekran kartında örnekleniyor, örneklerin dönüşümleri güncelleniyor), her 25 m'de dibe ip ve blok; büyük şamandıra ve
  45,9 m'lik halatı (3 kollu örgü, üst metrelerinde ~1300 midye), ayaklarda ~9700 midye.
- **B4 · süsler ve amforalar:** kayalık yuva (4 büyük blok aşağı yamaçta, 12 küçük), amfora yığını kayalara yaslı, 14
  tek tük amfora; sığda taş öbekleri, kırmızı deniz yıldızları, deniz hıyarları, yelpaze midyesi kabukları, kayalarda
  deniz kestaneleri.

**Sırada (o an):** B5 batık (yarı yıkık büyük kalyon, içine girilir), B6 sanat turu (renk ve kontrast düz: 10 m'de her şey
camgöbeği; ton eğrisi, güneş, pozlama), ölçüler, galeri, README.

**NE DOĞRULANMADI:** canlı pencerede yüzülmedi (yalnız komut satırı kareleri); kararlılık ve kare hızı yeni öğelerle
ölçülmedi (kare süreleri 25-45 ms arası görüldü: kostik 8-10 ms, kenar pikselleri yoğun yerlerde 10 ms); oksijen
zehirlenmesi akışı uygulamada denenmedi; dubaların hareketi karelerde değil yalnız kodda.


## Şu an · 2026-10-04 sabahı · Özcan sınadı: hüzme iyi, kemer ve kemere inen kum kaldırıldı

**Özcan (canlı baktı):** *"Hüzme daha iyi olmuş. Kemer kötü olmuş, kum de çevreyi düzenlemeden derine iniş çok kötü ve
yapay. O nedenle bu bir anda derine inen kumu ve kemeri silebilirsin."*

**Yapılan:** kemer sahneden ve koddan kaldırıldı (`koy_arch.*` silindi; uzaklık alanı ve surface nets git geçmişinde,
commit `a2a9378`); zemin profilinden tarladan inen yamaç ve kemerin arkasındaki duvar çıktı (arkadaki kıyıya doğru hafif
yükseliş kaldı). Sahne artık ~12 m'de kumluk bir düzlükte batık tarlası. Bunlara bağlı ayarlar sadeleşti: ışık alanı tek
dip (12,6 m; çok dipli yapı kodda duruyor, `light.bottoms`), kostik 0,5-14 m (6,9 ms), ince zemin ağı yalnız tarlanın
çevresinde, güneş görünürlük kutusu tarlaya küçüldü. Galeri ve README metinleri, galeri resmi ve README görselleri
kemersiz. Testler 11/11.

**Açmak:** `build\pc_graphics.exe` → galeride **05 Amfora**. Dalgıç tarlanın 9 m gerisinden başlar. Tuşlar: **W A S D**
yüz, **Boşluk** yukarı, **C** aşağı, **Shift** güçlü vuruş, **Tab** fareyle bakış, **P** durdur, **F** fener.

**Sıradaki (Özcan'ın seçimi):** kemerin yerine yücelik veren bir şey olsun mu (ör. tarlanın kenarında gerçek bir kayalık
resif, ya da yalnız ışık ve derinlik); amforaların rengi (gözün renk uyumu seçeneği); kum yakın planda 8 örnek.

## A7 · bitiş · 2026-10-04 gece

Galeride **05 Amfora** (ad, açıklama, altı başlıklı ayrıntılar, ArchAIDE atfı; panelde "Batık · 12 m"), yeni galeri resmi
(256 örnek), README EN/TR sahne 5 bölümü ve iki görsel, lisans bölümünde CC BY atfı. Otomatik test temiz (açılış 41,4
fps; kartta 12,1 GB; kapanınca bellek geri). Testler 11/11.

**Bağımsız gözden geçirme** (tek ajan, yalnız kod ve belge okudu; 11 bulgu, hepsi koda bakılarak doğrulandı):
1. *Işık alanı dibine yaklaşınca sönüyordu* (yukarıda A2'de): dibin altındaki düzlemler son aydınlık düzlemin değerini
   alıyor; bakış yolu geçişi tarlanın zeminini yutmayacak şekilde alanın dibinin 1 m altına kaydı.
2. *Kulp uçları açıktı ve gövdeye gömülmüyordu* (alt uç omzun 0,8-1,5 cm dışında, iki tipte üst uç ağzın boşluğunda):
   uç noktaları duvarın içine alındı, kesit uçlara doğru %65'e inceliyor, iki uç kapalı.
3. *Boyun parçasında kulp ayakları kırığın altında havadaydı:* kırık kulp ayaklarının altına indi.
4. *Bir yarım gövdenin topuk ucu açıktı:* parça boşluğun başladığı yüksekliğin üstünden başlıyor.
5. *Kubbe gibi yatan yarım gövdelerin kenarları kumun 1-5 cm üstündeydi:* yarım gövdeler ve parçacıklar ağın en alçak
   köşesine göre kuma oturuyor.
6. *README'de ölçülenden geniş aralıklar* ("3-10 kat", "13-15 kat"): ölçülen uçlar yazıldı (1,2-13 kat; kostikte 4,7-15).
7. *Dar bant işareti garanti etmiyordu* (alanın eğimi 1,6'yı aşıyor): pay 2,2 × köşegen + kabartmanın erimi (1,1 m);
   tam hesaplanan pay %19 → %34, kurulum 4,4 sn.
8. *Oluk katman başına seçilip alanda 6 cm'lik basamak yapıyordu:* düzlem başına (iki yanında aynı).
9. *Canlı lekeler kabuğa bağlı değildi:* kabuk maskesiyle çarpılıyor.
10. *Ortam kararmasında sıra bozulursa tampon dışı okuma riski:* o ağın kararması kapanıyor (şu an tetiklenmiyor).
11. *Bayat belge satırları:* "Şu an", kemer sayıları, açıklık (~12 m), README "32 örnek", ayar dosyası başlığı, bir yorum.
Emin olunmayanlardan biri (düşmüş blokların duvarın düştüğü yerde havada kalabilmesi) de giderildi: bloklar ayaklara yakın.

**NE DOĞRULANMADI:** Özcan'ın gözü (canlı pencerede yüzülmedi; her şey komut satırı kareleri, kararlılık yolları ve
otomatik test); düzeltmelerden sonra kararlılık yolları yeniden ölçülmedi (ışık alanı ve kulplar değişti; kostik ve
örnekleme aynı); kum yolunda 8 örnekle ölçüm yapılmadı (deneme alanındaki 30 cm'lik ölçüden çıkarım); kemerin dibine
30 cm'den 16K bakılmadı; gözün renk uyumu yalnız fikir.

## A2-A6 · zemin ve ışık alanı, amforalar, kemer, ışık, ölçüler · 2026-10-04 gece

**A2 · üç dip derinlikli ışık alanı.** Tek alan (dip 38 m) ile tek alan (dip 12,5 m) aynı pozlamada kıyaslandı: tarla
%34, kum %38, üstteki su %29 parlak (parlak kum ışığı yukarı yansıtıyor, bütün su sütunu aydınlanıyor). Şimdi ışık alanı
üç dip için (`light.bottoms = 12.6 16.6 38`: tarla, seki, duvarın dibi; hazırlık +3 sn). Zeminin derinlik haritası
(`KoyFloorShape`'ten 512²) her yüzeye altındaki dibi söylüyor; yüzey, dibi ayraçlayan iki alanı okuyor, her alanı kendi
dibinden derinde dibindeki değeriyle (eşyükselti boyunca dikiş yok, yamaçta kareyle bakıldı). Bakış yolları ilk 30 m'de
ulaştıkları derinliğe kadar aydınlık en sığ alanı alıyor (yolun ışığının ~%95'i oradan gelir, c ≈ 0,1/m), alanın dibine
1,5 m kala bir sonrakine yumuşak geçiyor. 🪤 İlk sürümde "aşağı bakan kaçan ışın = en derin alan" kuralı ufukta koyu
bir şerit çiziyordu (110 m'lik zemine çarpmayan, hafif aşağı bakan ışınlar); 30 m kuralı kaldırdı. Sonuç: tarla bölgesi
12,5 m'lik tek alanla aynı (0,99), kemer bölgesi seki alanıyla (0,89 / 12,5 m alanına). ⚠️ Bu kıyas bir kusuru
paylaşıyordu (A7'de bulundu, düzeltildi): alanın derinlik düzlemleri 0,635 m arayla, dibin altındaki düzlem sıfırdı ve
dibe yakın okuma ona karışıyordu (12,6 m'de ışığın %15'i). Düzeltmeden sonra kum, suya göre %8,5 daha aydınlık.

**A3 · amforalar.** ArchAIDE profilleri (dört tip, dış ve iç yüz, kulplar; atıf `assets/koy/LICENSES.md` ve README).
Genel parça üreteci (yükseklik ve açı aralığıyla duvar parçası, tırtıklı kırık, kırık yüzü duvar kalınlığı): tarlada 96
parça (sağlam amforalar, boynu kopmuş gövdeler tortuyla dolu, kulplu boyunlar, yarım gövdeler) + 40 çanak çömlek
parçası; dik olanlar omza kadar gömülü. Malzeme: `clay_plaster` 8K ayrıntı katmanı (2,00 m'lik tarama 1,64 m'lik karoya
basıldı: 0,2 mm tekstel, kil taneleri %18 küçük; plandan sapma), `coral_fort_wall_01` kabuk (0,8 m'lik karo: 1,36 m'de
damarları mermer gibi okunuyordu), gürültü maskesi (girintide çok, kumun aşındırdığı alt yüzde az), yukarı bakan yüzde
tortu keçesi, yanlarda yeşilimsi film, yavaş renk oynaması, birkaç cm'lik sünger ve kireçli alg lekeleri. 🪤 `KMaterial{}`
sıfırla başlıyor: `coatSet = 0` geçerli bir doku seti, bütün malzemeler istemeden kabuk alırdı; açıkça −1.

**A4 · kemer.** Hücresel gürültüyle 4,5 × 2,2 m'lik bloklar (dar çatlak, her blok kendi kayması), katmanların yalnız
bazısı çıkıntı ya da oluk, yassı tepe (`arch.flat = 0.55`). İlk denemede 2,3 × 1,25 m'lik hücreler taş örgü bir köprü
gibi okundu (karede), büyütüldü. Kemere 16K kaya ayrıntısı ve daha az kabuk.

**A5 · ışık (ilk tur).** Rüzgâr 2,5 → 4 m/s: sakin denizde sütunlar seçilmiyordu, yaz esintisiyle açıkça görünüyor
(üç kareyle kıyas; alçak güneş 42° sütunları düzleştirdi, 50° kaldı). Duvar yalnız kemerin arkasında, kenarı kıvrılıyor.
**Fener kararı (plan §3):** yapıldı, varsayılan kapalı (gündüz dalışı), panelde **Fener** ve **F**. Beyaz LED (5500 K
kara cisim), 1 m'de güneşin yüzeydeki aydınlatmasının %3'ü (2500 lm'lik dalış feneri ~3000 lx, öğle güneşi ~100 000 lx),
koni 20-32°, el gözün 25 cm sağında, gölge ışınıyla; ışığı suda gidip döner (yol uzunluğu tablosunda üç yeni satır).
Karede: 0,8 m'deki kırık amforanın kırık yüzü turuncu, sünger lekeleri kırmızı-turuncu; 2 m ötesi değişmiyor. Bedeli
yakın planda 20,4 → 24,7 ms. Fenerin sudaki kendi saçılımı (huzme, partiküller) yok.

**A6 · ölçüler.** Kostik 28 dilim, 0,5-18 m (seki 16-17 m; daha derini son dilimi alır): 10,3 → 7,2 ms. Kararlılık (60
kare, 256 örnekli referansa karşı, `--kararlilik`; yollar `--cam/--look` ile başlatıldı):

| Yol | Piksel ort / %99 | 8×8 blok ort / %99 | Bağıl | FLIP | Kare (o yolda) |
|---|---|---|---|---|---|
| amfora (tarlanın ~1 m üstünde süzülme, 0,5 m/sn) | 0,00166 / 0,0135 | 0,00023 / 0,0016 | 0,149 | 0,0090 | 19,9 ms |
| kemer (kemere yüzüş) | 0,00039 / 0,0036 | 0,00008 / 0,0004 | 0,306 | 0,0045 | 18,8 ms |
| yukarı (55° yukarı bakış, tarla) | 0,00059 / 0,0087 | 0,00016 / 0,0019 | 0,100 | 0,0047 | 27,1 → 21,2 ms |
| kum (kumun ~0,5-1 m üstü, aşağı bakarak) | **0,00444** / 0,0213 | 0,00052 / 0,0022 | 0,068 | **0,0206** | 29,9 ms |
| *Fener tabanı* | *0,0053-0,0061* | *0,0011-0,0026* | *0,45-1,24* | *0,026-0,038* | |

Hedef "her ölçüde Fener'in yarısından iyi": amfora ve yukarı dört ölçüde tuttu; kemer bağılda değil (0,306; uzaktaki
kemere yavaş yaklaşırken referansın kendisi az değişiyor, payda küçük; mutlak kıpırtı Fener'in 13 katı altında); **kum
pikselde ve FLIP'te tutmuyor** (Fener'in 0,8 katı). Kaynağı ayrıldı: rüzgâr değil (2,5 m/s ile 0,00441, aynı), kenar
eşiği 0,06 kötüleştiriyor (0,00505). Bu bakışta 16K kumun piksel altı tanesi 4 örnekte titriyor (deneme alanındaki kum
yolu 30 cm'den, taneler piksele göre iriydi: 0,00197). Bilinen çare 8 örnek (deneme alanında 0,00116), kareye ~5 ms;
varsayılan değişmedi.

**Kare hızı** (2560×1440, varsayılan 4 + 16, Ultra, dalgacık açık): kahraman bakış 20,0 ms, tarla 19,9, kemer 18,8,
yukarı 21,2 (tam iç yansıma aynasında görülen yüzeyler artık katmanların ortalamasıyla: temel geçiş 13,7 → 9,6 ms), kum
29,9 ms (33 fps: görüntünün %60'ı kenar pikseli; keskin hüzme kostiği kumda piksel altı karşıtlığı artırdı, deneme
alanında 1,39 milyon kenar pikseli vardı, şimdi 2,2). Uygulamada (otomatik test) açılış 41,4 fps, yüzerken 41,2, bazı
panel adımlarında 39,7-40,9. Planın öncelik sırasında kararlılık kare hızının önünde ve 30'un altına inilmiyor: kenar
örneği kısılmadı. **Bellek:** kartta 12,1 / 16 GB (hedef ≤ 14). Testler 11/11.

🪤 **Bu gece iki yolun ölçüsü sessizce bozuldu:** uzun ölçüm exe'nin kopyasından koşuyordu, ama cihaz kodu her açılışta
kaynaktan derleniyor; `KMaterial` büyüyünce eski kopya her alanı kaymış okudu (kumda mavi-yeşil lekeler: karesine bakınca
anlaşıldı). İki yol yeniden ölçüldü; artık ana kod ve cihaz kodu yapı boyutlarını kıyaslıyor, tutmazsa kare macenta.

## A0 · kaynaklar ve kompozisyon · 2026-10-04 gece · ✅ bitti

**Kaynaklar** (bir ajan araştırdı, ben okuyup karar verdim; tamamı `docs/koy/AMFORA_KAYNAK.md`, kareler `BASVURU.md`
§12-13): amfora **döner yüzey**, tarama değil. Girişsiz indirilebilen üç taramanın dokusu 1024² (1 m'lik nesnede 2,4-2,7
mm tekstel, hedefin 12 katı), ölçeksiz, ışık dokuya pişmiş; dokulu ve lisansı uygun bir Dressel 1B taraması yok.
Profiller ArchAIDE'in Southampton çizimlerinden çıkardığı vektörler (CC BY 4.0, mm): Dressel 1B (118 cm, MARQ'taki gerçek
bir 1B ile aynı boy), 1A, Dressel 2-4 İtalyan, Lamboglia 2; dış ve iç profil, kulplar. Malzeme: gövde Poly Haven
`clay_plaster` (2,00 m: 16K'da 0,122 mm), kabuk ve konkresyon `coral_fort_wall_01` (1,36 m, 8K'da 0,166 mm), kemer
depodaki 16K `rock_boulder_dry` (ek bellek yok) ya da ambientCG `Rock043S`. Kabuk canlıları için CC0 doku yok: kodla,
ölçü ve renkleri kaynaklı tabloda. Başvuru panosuna 15 batık + 8 kemer karesi; en öğreticisi Albenga (2-3 kat yığılmış
Dressel 1 tarlası), Tyre (üst yüzde tortu keçesi yandan ~4 kat parlak, gömülme çizgisi ~10 kat koyu).

**Yerleşim** (`scenes/koy.cfg` "dunya: Amfora"; kod `koy_arch.*`, `koy_amphora.*`, `KoyFloorShape`):
- Tarla: 12 m'de düzlük; ortada geminin gövde izi, 11 × 6 m, 45 cm'lik tümsek. Gemi ekseni kemere doğru (−75°): tarla
  bakışı kemere taşıyan bir çizgi. 96 amfora (A0'da kaba profiller, düz renk), sıralar gemiye dik, çoğu yan yatmış, kenarda
  ve uçlarda daha gömülü, birkaçı yarı dik.
- Kemer: uzaklık alanı (yarım elips eğri boyunca kalınlığı değişen kaya tüpü; bükme, düzensiz kireçtaşı katmanları, dikey
  çatlaklar, aşınma), iki yana alçalan kaya sırtları ve düşmüş bloklar; surface nets ile 7 cm hücre, 702 bin üçgen, 2,7 sn
  (dar bant: yüzeyden uzak noktalar kaba ızgaradan). Açıklık ~12 m geniş, ~7,5 m yüksek; tepe ~4,5 m derinlikte. (A0'daki
  sayı; A4-A5'ten sonra 767 bin üçgen, 4,4 sn: bloklar, daha güçlü bükme ve daha güvenli dar bant.)
- Kemer tarladan **19 m** ötede, **16 m'lik bir sekide**, arkasında zemin 36 m'ye düşen bir duvar.
- Güneş 50° yükseklik, azimut 165° (kemerin arkası, biraz sağ); suda dikeyden 29°.

**Kompozisyon kararları (karelerle, gerekçe):**
1. *Kemer 33 m'deydi, okunmuyordu:* kameradan ~41 m, IB suyunda karşıtlık e^(−0,1·41) ≈ %2; kubbe gibi bir gölge.
   19 m'ye çekildi (kameradan ~25 m, ~%8): tarlanın arkasında kemer olarak okunuyor. Plan "~30 m" diyordu; Özcan'ın
   *"biraz yüzmek gerekir"*i hâlâ geçerli (~20 m).
2. *Açıklıktan karanlık zemin görünüyordu:* taban 20 m, kamera 11 m: açıklığa yukarıdan bakılıyor, arkadaki zemin
   koyu. Taban 16 m'lik sekiye çıktı, arkası duvarla 36 m'ye düşüyor: açıklıktan açık su görünüyor ("maviye açılan kapı").
3. *Kemer arı kovanı gibiydi:* düzenli yuvarlak katmanlar; katmanlar yamalı (yumuşak yatak geri çekilmiş, sert yatak
   düz), eğik ve dalgalı, dikey çatlaklar, asimetrik ayaklar, eğik ve yassı tepe.
4. Kayanın rengi 0,24 (su altında kabuk ve alg bağlamış kireçtaşı; kuru taşın ~0,35'i): siluet koyulaştı.
5. Kahraman bakış: tarlanın ön kenarında alçak kamera, hafif yukarı (`--cam "1 -11.2 4" --look "2 -8.5 -20"`): önde
   amforalar, arkada kemer, tepede ışık sütunları.

**Okuma sırası:** şimdi tarla → kemer → sütunlar; sütunlar zayıf (11 m'de kamera, güneşe 50°'lik bakış). A5'in işi.
**En kötü 3 şey (kompozisyon):** (1) amforalar düz renkli plastik şişe gibi (A3: profil, kil, kabuk, tortu, kırık);
(2) kemerin yüzeyi pürüzsüz ve tepesi kubbe gibi yuvarlak (A4: doku, köşeli bloklar, sarkıntılar); (3) ışık düz ve mavi,
sütunlar seçilmiyor (A5).

**Bedel:** güneş alçalınca (70° → 50°) ışık suda daha eğik ve uzun yol alıyor, hüzmeler derinde daha çok örtüşüyor:
kostik 5,97 → 8,95 ms (tam çözünürlük bandı 13,5 m'ye çekildi). Hız turu A6'da; şimdilik 41 fps'in altına düşebilir.

**Ne doğrulanmadı:** kompozisyon yalnız durgun karelerle (pencerede yüzülmedi); ışık alanı tek derinlikte (dip 38 m):
tarlanın kendi kumundan yansıyan ışık eksik (A2'de iki derinlik); kemerin dibinde ve duvarda yakın plan yok.

## A1 · hüzme kostiği · 2026-10-04 gece · ✅ bitti

**Yöntem** (`koy_kernels.cu`, "caustics by beams"): yüzeyde 1024² hücrelik düzenli ağ; her hücre bir ışık hüzmesi.
Köşe ışınları kırılır (eğimler FFT alanından iki kübik ara değerlemeyle: hüzmelerin yoğunluğu alanın kendi hücrelerinde
basamak yapmasın). Her derinlik diliminde hücrenin dört köşesinin vardığı yerler iki üçgen kurar; üçgenin akısı alanına
eşit yayılır (yoğunluk = akı / alan, katlanan üçgenin alanı eksi, aynı formül). Her doku pikseli üçgenin kendi içinde
kalan alan payını **tam** alır: Green teoremiyle, satır satır, yalnız kenarların geçtiği sütunlar gezilir. Sayma yok,
gürültü yok; akı her dilimde yuvarlama kadar korunur (`--test kostik`: ortalama 0,9999). Odaklar küçük ve parlak
üçgen, dağılan hüzme büyük ve loş üçgen, katlanma çizgisi kendiliğinden keskin. Çok ince üçgen (alan < 0,001 doku²)
en uzun kenarı boyunca birkaç ikili doğrusal noktaya yayılır. Güneş diski bulanıklığı aynen kaldı.

**Dilim grupları** (ayar: `caustic.fine_z0`, `caustic.fine_z1`): 4,2 m'den sığda yalnız ışık sütunları okuyor (orada
duran bir şey yok): yarım çözünürlük, her ikinci hüzme. 4,2-15,5 m tam çözünürlük, bütün hüzmeler. 15,5 m'den derinde
güneş diski iki dokudan geniş bulanıklaştırıyor: yarım çözünürlük, bütün hüzmeler (katlanmalar oradaki ince ayrıntıdan
çıkıyor). Yarım dilimler bulanıklaştırmadan sonra tam çözünürlüğe çıkarılır; ışık sütunlarının piramidinde 1. düzey
yarım dilimin kendisidir.

**Gürültü** (canlı / referans: hüzme 2048², hepsi tam çözünürlük; aynı yol, sabit pozlama, kumdan 1 m, 45° aşağı, durgun
kamera; `kararlilik.py`):

| Derinlik | Hüzme (canlı) piksel ort / %99 | 8×8 blok | FLIP | Eski ışın 1× piksel ort | Kazanç |
|---|---|---|---|---|---|
| 7 m (deneme alanı) | **0,00084** / 0,0048 | 0,00058 | 0,009 | 0,0126 | 15 kat |
| 12 m (amfora tarlası) | **0,00123** / 0,0059 | 0,00077 | 0,014 | 0,0155 | 13 kat |
| 20 m (kemerin dibi, yarım grup) | **0,00285** / 0,0118 | 0,00252 | 0,027 | 0,0133 | 4,7 kat |

Hedef ≤ 0,003: üçünde de tuttu. Eski ölçü yeniden üretildi (ışın 1× / ışın 16×: 0,0121; geçen ölçüm 0,0113). Yan bulgu:
eski referans (16 kat ışın) da gürültülüymüş: hüzme referansına karşı 0,0033, yeni canlı yöntemin 4 katı. 20 m'deki
farkın çoğu blokta da var (yumuşak): yarım çözünürlüğün hareketli desende biraz farklı bulanıklığı, gren değil.

**Süre** (`--test kostik`, dünyanın bir anı): kostik **5,97 ms** (hüzmeler 4,84, bulanıklaştırma 0,80, gerisi hazırlık
ve piramit). Eski ışın yolu artık 3,47 ms (aynı kopya tasarrufuyla; önce 4,75). Uygulamada (otomatik test) 41 fps sabit.
Yol: ilk sürüm 61 ms'ydi; ölçerek indirildi:
- Satranç tahtası köşegeni için iki ayrı çağrı noktası her warp'ı ikiye bölüyordu: tek çağrı, köşeler seçimle (61 → 32).
- Çerçeve kutusu yerine satır satır yalnız kenarların sütunları (pencere 12 m'de hücre başına 24 doku, gerçek alan 5,6).
- Dilim grupları (yukarıda) ve her blok tek dilim: dilim (4 MB) L2 önbellekteyken dolar; tek iş parçacığının bütün
  dilimleri dolaşması %30 yavaştı.
- `__launch_bounds__(128, 8)` (64 yazmaç): 5,1 → 4,8 ms; daha sıkısı taşırdı (8,6 ms).
- Bulanıklaştırma ağırlıkları blok başına bir kez (her dokunuşta `expf` ve tam sayı bölmesi vardı): 1,25 → 0,80 ms.
- Dilimler doğrudan 3B dokuya (yüzey nesnesi) yazılır, ara tampondan kopya ve piramit tamponları kalktı; bellek
  sıfırlaması yalnız hüzmelerin doldurduğu dilimlerde.
- 🪤 Ölçüm kendini kirletiyordu: dilim başına sayaçlar her hücrede aynı 6 adrese atomik ekliyordu, her dilime 2 ms'lik
  taban ekledi. Sayaç ve zamanlama ayrı kiplere ayrıldı (`--set test.beam_stats=1` sayaç, `=2` dilim dilim süre).

**Panel:** "Kostik ışını 1×/4×/8×/16×" kalktı; yerine **Kostik: Hüzme / Işın (eski)** (iki yöntem canlı geçişli,
Özcan farkı görebilsin). Ayrıntılar metni hüzmeyi anlatıyor.

**Bulunan eski hata:** `--test` tam takımda dalgacık testi hep kalıyordu (tek başına geçiyordu): Snell testi düz deniz
için `ocean.amplitude=0` koyup uygulamayı yeniden kuruyor, geri almıyor; sonra gelen dalga testi düz denizi ölçüyordu.
Snell sona alındı; tam takım 10/10.

**Ölçü araçları:** `--kararlilik ... --canli` (yalnız canlı geçiş), `--cam/--look` artık kararlılık yolunun başlangıcı,
`display.exposure_fixed` kararlılık yolunda da. İki koşunun canlı karelerini kıyaslamak: scratchpad'deki `kostik_olc.py`
yöntemi (live_* birinci koşu, ref_* ikinci koşunun live_*'ı).

**Ne doğrulanmadı:** kostik gürültüsü yalnız durgun kamerayla ölçüldü (yüzerken değil); ışık sütunlarında yarım
çözünürlüklü sığ dilimlerin farkı kareyle kıyaslanmadı; 25 m'ye inen dilimler (plan §5.2) henüz yok, A2'de; Özcan'ın
gözü (yöntem farkı) yok.

## Ara durak 3 kararı · 2026-10-04 gece · ✅ geçti, dalgacıklar kalıyor; kostik gürültüsü ölçüldü

Özcan (düzeltilmiş dalgacıklara canlı baktı): *"Daha güzel oldu. Daha tanecikli ve aslında daha az stabil ama diğeri daha
oyuncak gibi basitti. Bu şekilde daha gerçekçi oldu. Gerçekte de su biraz pütürlü ışığı noktacıklı yansıtan halde olur."*
Dalgacık varsayılan açık kalıyor. Düzeltilmiş sürümün kare süresi (ekran kartı boşken): yukarı bakarken 21,7 ms (46 fps;
dalgacıksız 20,6), yüzme görünümünde 19,2 ms.

**Kostik gürültüsü (kararlılık ölçüsünün kör noktası, ölçüldü):** canlı ve referans aynı kostik hacmini paylaştığı için
kostiğin kendi Monte Carlo gürültüsü kıpırtı ölçüsüne hiç girmiyordu. Ayrı ölçüldü: aynı durgun görünüm (kumdan 1 m, 45°
aşağı, 60 kare) bir kez şimdiki ışınla (1024², dokuya ~1 ışın), bir kez 16 kat ışınla; canlı = şimdiki, referans = 16 kat.
Kıpırtı piksel ort / %99 **0,0113 / 0,042**, 8×8 blok **0,0109 / 0,040**, bağıl 0,52 (aynı görünümün olağan kıpırtısı
0,00032). Blok ≈ piksel: tanecik değil, yumuşak lekeler hâlinde kaynama. Kostik zaten hızlı dans ediyor (dibi ~0,7 m/s
süpürüyor; 16 kat ışınla ikinci zaman farkı 10,7 gri RMS), gürültü buna ~yarısı kadar ek kaynama katıyor (12,4). Işınların
sarsıntısı sabit (her kare aynı), yani sorun yeniden rastgeleleşme değil, dokuya düşen ışın sayısı. Ucuz çare yok: 4 kat
ışın gürültüyü yarıya indirir ama kostik 4,7 → 9,5 ms; kare yüzerken 19,2 → 24,0 ms (41,6 fps, sınırda), yukarı bakarken
21,7 → 26,5 ms (37,7 fps). Doğru çözüm gürültüsüz bir yöntem: her yüzey hücresinin ışığını nokta yerine hüzme olarak
dibe yaymak (alan değişimi ya da ışın türevli çekirdek). Önerim: amfora planına.

**Görmek için panelde "Kostik ışını: 1× / 4× / 8× / 16×"** (Özcan istedi: *"FPS çok düşsün ama [...] Amaç sadece gürültüsüz
nasıl oluyormuş o farkı görmek"*). Işın sayısı yalnız ışın atma çekirdeğinin başlatılmasında ve ağırlıkta, canlı değişir;
durmuşken basılınca kostik hemen yeniden hesaplanır. Yüzme görünümünde kare: 1× 19,2 ms, 4× 24,0, 8× 32,7 (30,6 fps; kostik
18,5 ms), 16× 51,4 ms (19,5 fps; kostik 36 ms); uygulamada 16× 21,8 fps. Yan bulgu: ışın arttıkça kenar pikseli 200 bin →
140 bin: kostik gürültüsü kenar algılamayı da boşuna tetikliyormuş.

## Ara durak 3 · 2026-10-04 gece · yüzeyin dalgacıkları (geçti)

Özcan: *"kostik ve su yüzeyindeki o küçük dalgacıkları yapmak kolay mı? Zorsa amforaya dahil edelim. Kolaysa yapalım,
bakayım."* Altyapının çoğu vardı (FFT sınıfı, LEAN eğim piramidi), yapıldı.

**Dalgacık katmanı:** ana yüzeyin bilerek kestiği kısa dalgalar (35 cm altı, 2 cm üstü) ikinci bir FFT'de, kendi 4 m'lik
karosunda (1024², 3,9 mm'de bir nokta), aynı JONSWAP biçimiyle. Yalnız bakışa girer: Snell penceresi, tam iç yansıma
aynası, güneş pırıltısı; yüzeyle kesişmeye ve kostiğe girmez (kostik temiz 35 cm yüzeyde kalır). Gücü, eskiden
istatistiksel olan eğim varyansının %80'i (eksen başına 0,0005); o kadarı istatistikten düşülür, 2 cm altı istatistikte
kalır. Uzaktan ayak izi onları aynı pürüzlülüğe ortalar (LEAN): toplam aynı, yalnız yakında artık çözülüyor. Panelde
**Dalgacık: Kapalı / Açık** (varsayılan Açık). Ölçü: `--dalgacik` (aynı an, tek pozlama), `tools/doku_kiyas.py --kipler
kapali,acik`.

🪤 **İlk sürümde dalgacıklar iki kat güçlüydü** (bağımsız gözden geçirme yakaladı, ekran kartında ölçülerek doğrulandı):
FFT alanı Hermitsel kurulur (H(k) = h0(k)e^{iωt} + conj(h0(−k))e^{−iωt}), her h0 alanda iki kez görünür, ama `KoyOcean::init`
varyansı bir kez sayıyordu. Dalgacıkların gücü bu deftere göre ayarlandığı için gerçek eğim varyansları 0,00097 çıktı
(hedef 0,00048); uzaktan bakınca yüzey olması gerekenden ~1,8 kat pürüzlüydü. Düzeltildi; yeni test `--test dalga` alanı
deftere karşı ölçer (dalgacık 1,005, ana yüzey 0,984: geçti). Ana yüzeyin kendisi değişmedi, yalnız günlükteki sayıları
doğrulandı (çözülen eğim varyansı 0,0056 değil 0,0102; Hs 0,191 değil 0,202 m; kesişme bandı 0,235 → 0,248 m). Özcan'ın
00.12'de açtığı pencere eski (iki kat güçlü) sürümdü; uygulamayı yeniden açınca düzeltilmiş sürüm gelir.

Düzeltilmiş dalgacıklarla (karşılaştırma ve kararlılık `tex.high=0` ile, Özcan'ın penceresi açıkken bellek için; kapalı
ve açık aynı ayarla):

| Görünüm | FLIP kapalı→açık | Ekrandaki en ince ayrıntı kapalı → açık |
|---|---|---|
| 1,5 m derinlik, 45° yukarı | **0,071** | 0,88 → 1,11 |
| 4,6 m, 45° yukarı (pencere kenarı) | **0,049** | 0,84 → 1,25 |
| 4,6 m, 20° yukarı (ayna) | 0,031 | 0,55 → 0,58 |
| Yüzme görünümü (aşağı) | 0,001 | aynı |

Kırpıntılarda fark sayının söylediğinden büyük: kapalıyken pencerenin kenarı çizilmiş gibi pürüzsüz ve kenar bantlı,
açıkken gerçek su altı çekimlerindeki gibi kırılıp parçalanıyor; ayna ince ince bükülüyor. (İki kat güçlü sürümle FLIP
0,080 / 0,052 idi: görünen etki varyansla orantılı değil, kritik açı yakınında küçük eğimler bile kenarı kırıyor.)

**Kararlılık** (`--yol yukari`, 60 kare): piksel kıpırtısı ort / %99 kapalı 0,00057 / 0,0056 → açık 0,00084 / 0,0067; blok
0,00013 → 0,00018; bağıl 0,079 → 0,110; FLIP 0,0049 → 0,0063. Kıpırtı ~1,5 kat (iki kat güçlü sürümde 1,9); Fener'in ~6
kat altında, K0 hedefinin içinde.

**Bedeli** (iki kat güçlü sürümle ölçüldü, ekran kartı boşken; düzeltilmişte yeniden ölçülmedi, en çok bu kadar olmalı):
yukarı bakarken kare 20,6 → 23,9 ms (41 fps'in 24,4 ms sınırına çok yakın): temel geçiş +2,7 ms (aynadan dağılan ışınlar
daha az uyumlu), kenar pikselleri 17 bin → 86 bin. Aşağı bakarken fark yok (otomatik testte 41 fps).

**Kostik, yakından:** 30 cm'de dört ayarla kıyaslandı (aynı pozlama): 1K ızgara + 1 ışın/hücre (şimdiki), 4 kat ışın, 2K
ızgara. Izgaranın düz ara değerlemesi eksene paralel çok soluk çizgiler bırakıyor (parlaklıkta ~%1,3, FLIP 0,043); ışın
sayısı ~%3,4'lük yumuşak lekeler (FLIP 0,086). İkisi de kırpıntılarda gözle seçilmiyor. Kaba kuvvet çözüm (2K) kostiği
4,7 → 23,5 ms yapıyor: olmaz. Özcan canlıda düz çizgi ya da leke görürse ucuz yol: kostikte yumuşak (kübik) ara değerleme.

### Ara durak 3'te neye bakılacak
Space ile yüzeye doğru çık (tavan 1 m), Tab ile fareyle 30-60° yukarı bak: Snell penceresinin kenarı ve ötesindeki ayna.
Panelde **Dalgacık: Kapalı ↔ Açık**. Sonra 4-5 m'de aynı bakış. Kıpırtıya ve kare hızına dikkat (yukarı bakarken 41'in
altına düşebilir). Bir de kuma 30 cm'den bakıp kostik ağlarında düz çizgi ya da leke var mı. Karar: dalgacık kalıyor mu?

**Ne doğrulanmadı:** düzeltilmiş dalgacıklara canlı bakılmadı. Hareket hâlinde (yüzerken) kıpırtı ölçülmedi, yalnız durarak
yukarı bakış. Düzeltilmiş sürümün kare süresi ölçülmedi. Desenin 4 m'de bir tekrar edip etmediği gözle aranmadı.

## Ara durak 2 kararı · 2026-10-03 gece · ✅ geçti, 16K kalıyor

Özcan canlı baktı: *"Kesinlikle farketti. Geceyle gündüz değil, şafakla gün batımı gibi farkedilme olarak. Net fark var ama
hani oha kadar değil. Ama 16K olsun o konuda şüphe yok."* Ultra varsayılan kalıyor. Ölçüm de aynını söylüyordu: 30 cm'de
Yüksek→Ultra, Standart→Yüksek adımının ~yarısı (FLIP 0,133 / 0,288). Sıradaki: amfora tarlası planı ("başla" bekleniyor);
sohbette suyun kendi çözünürlüğü konuşuluyor.

## Ara durak 2 · 2026-10-03 gece · 16K, panelde "Ultra" (geçti)

Ara durak 1 (aşağıda) geçti: *"Geceyle gündüz gibi fark var"*. Sonra her şey 8K yapıldı. Özcan sordu: her şey 16K olabilir
mi? Cevap ölçüyle verildi (aşağıda "Kaynak"); Özcan: *"Kum + atlas 16K olsun [...] Yüksek yanına ultra diye buton koy [...]
inanç değil ölçümle yapalım"*. Monitörü 32,5 inç, 2560×1440, yakından bakıyor.

**Kaynak (indirildi ve sınandı):** ambientCG Ground054 16K (2,1 GB zip, boyutu API ile aynı) ve Poly Haven rock_boulder_dry
16K (md5 tuttu). İkisi de gerçek 16K, büyütülmüş 8K değil: aynı fiziksel yamada 8K'nın gösteremediği yeni ince bandın gücü
(gri seviye RMS) gerçek 16K'da kum rengi 7,0, kum normal haritası 19 (alttaki bantlar kadar), atlas 6,0; aynı yamanın
Lanczos'la büyütülmüş 8K'sında 2,1 / 3,8 / 2,8. Kayanın kendi taraması (boulder_01) 8K'da bitiyor; amfora ve kemer adayları
için 16K kaynak yok ya da fotoğraflanan alanları küçük (1-2 m), 8K zaten ekran sınırında.

**Yapılanlar:**
- **Panelde "Doku: Standart / Yüksek / Ultra".** Ultra = Yüksek'in aynısı, yalnız kum ve kayanın ayrıntı katmanı 16K (aynı
  karo boyları: kum 3,5 m taban + 1,75 m kendi tanesi, atlas 1,8 m). Üçü de bellekte, geçiş anında. Varsayılan Ultra; 16K
  dosyaları yoksa Yüksek'e, 8K yoksa Standart'a düşer (panel düşülen kipi gösterir, uyarı çıkar).
- **Paralel yükleme:** her haritanın çözülmesi ve mip zinciri kendi iş parçacığında, GPU'ya yükleme sırayla; 1 GB'lık çözme
  tamponu kopyalanmadan seviye 0 olur. 16K dahil yedi set **7,2 sn**; açılış yine ~10 sn (eskiden 8K'lık set tek iş
  parçacığında yükleniyordu, açılış ~11 sn idi). Süreç RAM tepesi 6,0 GB.
- **Dibe 30 cm:** dalgıcın gözü kuma en çok 0,3 m yaklaşır (eskiden 0,6; kaya için zaten 0,3). Bu olmadan 16K kumun
  farkı ekranın ortasında görünmüyordu (0,6 m'de bir piksel kumda 0,35 mm, 8K tekstel 0,43 mm: 8K zaten sınırda).
- **Kenar listesi bütün ekran:** 30 cm'den 16K kumda pikselin %38'i kenar sayılıyor (alt piksel tane karşıtlığı); liste
  ekranın üçte birinde kesiliyordu ve atomik sıraya göre her karede başka pikseller dışarıda kalıyordu (kıpırtı). Liste
  artık her piksele yer açar (+0,35 GB).
- **Ölçü araçları:** `--doku` (aynı an, üç kip, tek pozlama: yalnız dokular farklı) + `tools/doku_kiyas.py` (FLIP Özcan'ın
  ekranı ve uzaklığıyla, ekrandaki en ince oktavın gücü, kırpıntılar); `--yokla` (kumdan yükseklik, en yakın yüzey);
  kararlılık yolu `--yol kum` (kumun 30 cm üstünde, aşağı bakarak ileri yüzme).

**Ölçümler** (2560×1440, 4 örnek + kenar 16; FLIP 32,5 inç ekrana 50 cm'den: 31,1 piksel/derece, FLIP'in kendi bildirdiği
değerle doğrulandı; "en ince ayrıntı" = merkezdeki 1024²'de ekranın en ince oktavının gri seviye gücü):

| Görünüm | Ekrandaki en ince ayrıntı: Standart / Yüksek / Ultra | FLIP Yüksek→Ultra | FLIP Standart→Yüksek |
|---|---|---|---|
| Kum, 30 cm, aşağı bakış | 0,42 / 1,76 / **3,62** | **0,133** | 0,288 |
| Kum, 60 cm, aşağı bakış | 0,56 / 4,69 / **6,80** | **0,079** | 0,251 |
| Kum, 1 m, 45° | 2,25 / 6,48 / 6,52 | 0,028 | 0,128 |
| Yüzme görünümü (kumdan 2 m) | 0,84 / 0,86 / 0,86 | 0,004 | 0,013 |
| Kaya, 31 cm (ayrıntı katmanı) | 0,41 / 0,78 / 0,79 | 0,007 | 0,094 |
| Kaya, 36 cm | 0,40 / 0,55 / 0,57 | 0,009 | 0,091 |
| Kaya, 1,1 m | 0,60 / 1,32 / 1,32 | 0,005 | 0,048 |

Ölçek: Standart→Yüksek, Özcan'ın "gece-gündüz" dediği adım. **Kum:** Yüksek→Ultra 30 cm'de o adımın ~yarısı (0,133 /
0,288), 60 cm'de ~üçte biri, 1 m'de ~beşte biri; ekrandaki en ince ayrıntı 30 cm'de 2 kat. Uzakta fark yok. **Kaya:**
Yüksek→Ultra o adımın ~onda biri: 16K atlas 31 cm'de bile görünmüyor (8K atlasın teksteli 0,22 mm, 30 cm'de bir piksel
0,175 mm: 8K zaten sınırda); kayanın görünüşünü kendi 8K taraması belirliyor.

🪤 İlk sürümde `doku_kiyas.py` FLIP'e ekranı ters sırayla veriyordu (`vc` = [uzaklık m, genişlik PİKSEL, genişlik m];
FLIP sessizce ~0 piksel/derecede çalıştı); bağımsız gözden geçirme yakaladı, betik artık ppd verir ve FLIP'in kullandığını
denetler. Sonuçlar aynı yönde, sayılar %10-25 düştü.

**Kararlılık** (`--yol kum`, 60 kare; kıpırtı yakınsamış referansa karşı):

| | Piksel kıpırtısı ort / %99 | 8×8 blok ort / %99 | Bağıl | FLIP | Kare (bu görünüm) |
|---|---|---|---|---|---|
| Yüksek, 4 örnek | 0,00088 / 0,0056 | 0,00016 / 0,00067 | 0,0098 | 0,0070 | 10,3 ms |
| Ultra, 4 örnek | 0,00197 / 0,0098 | 0,00028 / 0,00123 | 0,0205 | 0,0111 | 12,4 ms |
| Ultra, 8 örnek | 0,00116 / 0,0062 | 0,00019 / 0,00069 | 0,0121 | 0,0083 | 17,2 ms |
| *Fener tabanı* | *0,0053-0,0061 / 0,050-0,080* | *0,0011-0,0026* | *0,45-1,24* | *0,026-0,038* | |

Ultra yakın kumda daha çok ince ayrıntı taşıdığı için 4 örnekte ~2 kat kıpırdıyor; yine de Fener'in ~3 kat altında ve K0
hedefinin içinde. 8 örnek kıpırtıyı Yüksek'e yakın düzeye çekiyor.

**Bedeli:** doku dizileri GPU'da 7,44 GB (16K'sız 2,10 GB, hesap); kartta toplam 10,5 / 16 GB dolu (masaüstü dahil,
ölçüldü). Kare: yakın kumda Yüksek 13,9 → Ultra 15,9 ms (kenar pikselleri 0,63 → 1,39 milyon); yüzme görünümünde fark yok
(18,2 ms); uygulamada (otomatik test) üç kipte de 41 fps sabit.

### Ara durak 2'de neye bakılacak
Galeride 05 Koy (açılış ~10 sn). Panelde **Doku** satırı: Standart / Yüksek / Ultra. **C** ile kuma in (göz 30 cm'ye
kadar iner), Tab ile fareyle aşağı bak. Aynı yerde Yüksek ↔ Ultra ↔ Standart arasında geç. Sonra kayaya sokul (30-40
cm) ve aynı geçişi yap. Bir de yakın kumda yavaşça yüzerken Ultra'yı **Örnek 4** ve **8** ile kıyasla (kıpırtı). Karar:
Ultra kalıyor mu, Yüksek mi?

**Ne doğrulanmadı:** Özcan'ın gözüyle canlı bakılmadı (ölçümler kare ve kare dizisi üzerinden). FLIP'teki 50 cm oturma
uzaklığı varsayım. Kararlılık yalnız kum yolunda ölçüldü; kayada Ultra'nın kıpırtısı ölçülmedi (görüntü farkı yok denecek
kadar az). Doku sıkıştırma (BC7/BC5) yok, 16K sıkıştırmasız duruyor.

## Ara durak 1 · 2026-10-03 gece · yüksek çözünürlüklü doku (geçti)

**Durak 1 geçti** (Özcan canlı baktı): *"En düşük seçeneklerde 1 e 4 [...] gayet iyi [...] her şekilde net. Ayar
bağımsız"*, en düşük ayarda 90 fps; *"Suyun maviliği çok güzel. Beğendim okey"*. Tek eleştiri: kaya yakından düşük
çözünürlüklü (*"uzaktan wow dersin yakından kötü"*). Önerisi: örnekleri düşük tut, dokuya yüklen; *"Beğenmezsek geri
alırız [...] ara küçük bir durak ekleyelim"*. Koy (K2) bu durağa kadar başlamaz.

**Bu durakta yapılan:**
- **Teşhis:** kaya 2K taramaydı ve 2,8 m'ye büyütüldüğü için doku pikseli ~3-4 mm; 1 m'den bakınca ekrandaki bir piksel
  ~0,6 mm (maske camı 4/3 yaklaştırır): her doku pikseli ~6 ekran pikseli. Kum 2K / 3,5 m: ~1,7 mm.
- **Yüksek doku seti** (`assets/koy/fetch.py`, Poly Haven md5'leri doğrulandı): kaya aynı taramanın **8K**'sı (~0,8 mm),
  üstüne dünya uzayında **ayrıntı katmanı** (Poly Haven rock_boulder_dry 4K, 0,9 m karo: ~0,2 mm; kayada üç düzlemli
  izdüşüm, rengi kendi ortalaması çevresinde dalgalandırır, yüzey haritası normali büker), kum **4K** (~0,85 mm) ve kendi
  tanesinden 0,9 m'lik ayrıntı katmanı. Dokular ışın konisiyle süzüldüğü için fazla çözünürlük uzakta kıpırtı yapmaz.
- **Panelde "Doku: Standart / Yüksek"** (canlı karşılaştırma; ikisi de bellekte, geçiş anında).
- **Kayaya yaklaşma:** dalgıcın kaya çarpışması artık 26 yöne ışın yoklamasıyla (baş yarıçapı 0,3 m); eskiden kaba bir
  küre dalgıcı kayadan ~0,5-1,5 m uzak tutuyordu. Çayır yoklamaya girmez (eğilir).
- **1 örnekte kenar algılama:** tek örnekte piksel içinde karşılaştıracak fark yoktu, kenar iyileştirmesi hiç devreye
  girmiyordu; artık 1 örnekte piksel dört komşusuyla karşılaştırılır (kenarın iki yanı da işaretlenir).
- **Bedeli:** bellek +~1,1 GB (sıkıştırmasız; toplam ~4,3 GB / 16), yükleme ~5 → ~8 sn, kare ~18 → ~21 ms (4 örnek; ayrıntı
  katmanının örnekler arası karşıtlığı daha çok pikseli kenar sayıyor), 1 örnek + kenar 4: ~11 ms.

**Yakın plan ölçüsü** (yeni yol `--yol yakin`: kayanın ~1 m önünde, ona bakarak yan yan kayma, yüksek doku, 100 kare):

| | Piksel kıpırtısı ort / %99 | 8×8 blok ort / %99 | Bağıl | FLIP |
|---|---|---|---|---|
| 1 örnek (+ kenar 16) | 0,0016 / 0,012 | 0,00029 / 0,0019 | 0,084 | 0,0093 |
| 4 örnek (+ kenar 16) | 0,0006 / 0,0059 | 0,00013 / 0,0008 | 0,031 | 0,0051 |
| *Fener tabanı* | *0,0053-0,0061* | *0,0011-0,0026* | *0,45-1,24* | *0,026-0,038* |

İkisi de Fener'in çok önünde; ayrıntılı dokuda yakın planda 4 örnek 1'e göre ~2,7 kat sakin, ~1,8 kat net (uzaktaki
genel görünümde fark küçüktü, Özcan'ın gözlemi de bu). Uygulamada yüksek dokuyla 41 fps sabit (kare 19,6 ms; standart
dokuda 16 ms).

**Her şey 8K (Özcan istedi, ara durak onaylandıktan sonra):** kum ambientCG Ground054 8K (3,5 m karo: ~0,43 mm), ayrıntı
katmanı Poly Haven rock_boulder_dry 8K (1,8 m karo: aynı ~0,22 mm netlik, tekrar yarıya iner; kumun kendi ayrıntısı 1,75 m),
kaya atlası zaten 8K. "Yüksek" seçeneği artık bunları okur; 4K setler yüklenmez. Kare ~21 ms (41 fps sabit), açılış ~11
sn, bellek tahmini ~5,3 GB / 16 (sıkıştırmasız).

### Ara durakta neye bakılacak
Galeride 05 Koy. Kayaya yaklaş (W, gerekirse Space/C; Tab ile fareyle bak), 30-50 cm'ye kadar sokul. Panelde **Doku:
Standart ↔ Yüksek** geçişiyle aynı yere bak. Sonra kumun dibine yaklaş (C ile alçal). Karar: yüksek doku kalıyor mu, geri mi
alınıyor? Örnek sayısı: 1 + kenar 4 ile de bak (yakın planda kıpırtı ölçüsü aşağıda).

## Durak 1 · 2026-10-03 gece · netlik ve kararlılık (geçti)

K0 bitti, K1 (deneme alanı) bitti. Koy kurulmadı: plan gereği Durak 1 geçilmeden koy kurulmaz.

### Durak 1'de neye bakılacak (Özcan, yalnız uygulamanın içinden, canlı)

Açmak: `build\pc_graphics.exe` → galeride **05 Koy** (ya da `pc_graphics.exe --scene koy`). İlk açılış ~5 sn (ışık
alanı hesaplanıyor). Tuşlar: **W A S D** yüz, **Boşluk** yukarı, **C** aşağı, **Shift** güçlü vuruş, **Tab** fareyle bakış
(aç/kapa) ya da **sağ tık basılı** bak, **P** zamanı durdur (durunca 256 örneklik resim birikir), **E** kenar piksellerini
göster, **K** kostik aç/kapa, **↑↓** pozlama.

1. **Dur ve bak (5-10 sn):** dalgalar, kostik ağı, ışık sütunları, çayır akıyor; kamera durgun. Kumda, kaya kenarında,
   çayırda, yüzeyin alt yüzünde karıncalanma, dalgalanma, titreşme var mı? (Ölçüde Fener'den ~15 kat sakin.)
2. **Yavaşça yüz ve dön:** W ile kayaya, çayırın kenarına doğru; Tab ile fareyle yavaşça dön. Kenarlar (kaya silueti,
   çayır yaprakları, kumla kaya temas çizgisi) kayarken tırtıklanma ya da sürünme var mı?
3. **Yukarı bak:** yüzeyin alt yüzü, ayna gibi tam iç yansıma ve tepede Snell penceresi; pırıltılar titreşiyor mu?
4. **Panel düğmeleri** (sol alt): **Örnek** 1 / 4 / 8 / 16 (piksel başına sabit örnek), **Kenar** 4 / 8 / 16 / 32 (kenardaki
   piksele toplam), **Süzgeç** Kutu / Blackman-Harris / Mitchell / Gauss. Varsayılan 4 + 16 + Blackman-Harris. 1 örnek ile
   4'ü kıyasla: fark netliğin kendisi. Kare hızı sol üstte (RivaTuner) ya da panelin sağ üstünde.
5. **P (dur):** zaman durunca bir an sonra 256 örneklik resim gelir; canlı resimle farkı buradan görülür.

Karar istenenler: (a) varsayılan örnek ve süzgeç yeterince net ve sakin mi; (b) kenar 16 mı 32 mi (32: ~41 fps sınırında);
(c) bir şey göze batıyorsa ne ve nerede.

## Yapılanlar

### K0 · ölçüler hazır
- **Ölçü düzeneği** (`tools/kararlilik.py`): bir kamera yolunun canlı kareleri (pencerenin çizdiği yoldan, aynı örnek,
  aynı süzgeç) ve aynı anların yakınsamış referansı. *Kıpırtı:* canlının kareden kareye değişiminden referansın kendi
  değişimi düşülür (kamera ya da dünya kıpırdarken gerçek değişim cezalanmaz), piksel ve 8×8 blok düzeyinde. *Netlik:*
  FLIP (NVIDIA). *Bağıl kıpırtı:* kıpırtı / referansın değişimi. Isı haritası da yazılır.
- **Fener tabanı** (Fener'e `--kararlilik` eklendi; canlı: TAA + titreşim + göz uyumu; referans: 128 kare birikim):

  | Yol | Piksel kıpırtısı ort / %99 | 8×8 blok ort / %99 | Bağıl | FLIP |
  |---|---|---|---|---|
  | Fener A (kamera durgun, huzme döner) | 0,0053 / 0,050 | 0,0011 / 0,0125 | 1,24 | 0,038 |
  | Fener yolculuk (orman içi yürüyüş) | 0,0061 / 0,080 | 0,0026 / 0,036 | 0,45 | 0,026 |

  Kıpırtı Fener A'da denizdeki ay pırıltılarında, yolculukta ağaç gövdelerinin kenarında ve uzak ormanda toplanıyor
  (ısı haritası). **Hedef (K0'da kondu):** Koy her ölçüde Fener'in yarısı ya da daha iyi.
- **Veri** (`docs/koy/VERI.md`): beş araştırma ve iki bağımsız denetim. Pope-Fry'ın 140 değeri, Solonenko-Mobley Jerlov
  tipleri, Austin-Petzold Kd, Fournier-Forand, Zaneveld-Pegau görüş ilişkisi, ASTM G173 güneş tayfı, kabarcık yükselme
  hızları (Haberman-Morton) ve Bühlmann ZHL-16C. `koy_data.h` bu JSON'dan betikle üretildi (elle aktarım yok).
- **CC0 indirme betiği** `assets/koy/fetch.py`: adres + SHA-256; kum (ambientCG Ground054), kaya (Poly Haven boulder_01).
  K2 için seçilmiş olanlar (çakıl, iki kaya yüzeyi, iki kaya modeli) `VERI.md`'de.
- **Başvuru panosu** `docs/koy/BASVURU.md`: 35 gerçek fotoğraf (bağlantı + not), on bölge; her biri indirilip bakıldı.
- **DLSS / DLAA denemesi:** araştırıldı, bağlanmadı. CUDA yolu yalnız **DLSS Ray Reconstruction**'da var (SDK 310.9.1,
  sürücü 590+; NVIDIA'nın OptiX örneği `optix-subd` varsayılanı DLAA). Super Resolution'ın (DLAA dahil) CUDA yolu yok
  (yalnız D3D11/12 ve Vulkan). Ray Reconstruction zamansal ve yapay zekâlı bir gidericidir: planın "zamansal tahmin yok"
  ilkesine ve Özcan'ın TAA yargısına ters. Ana yol kaba kuvvet süper örnekleme; istenirse Durak 1'den sonra yan yana
  kıyas için bağlanabilir (lisans: RTX SDK, DLL betikle indirilir, depoya girmez).

### K1 · deneme alanı
Çizici `src/scenes/koy/` (mimari özeti `PLAN.md` §5'teki gibi; aşağıda yalnız plandan ayrılanlar ve ölçümler).

- **Örnekleme:** her pikselde aynı sabit alt piksel örnekleri (4: döndürülmüş ızgara; 8, 16: Direct3D'nin MSAA desenleri),
  titreşim yok, geçmiş yok. Kenar piksellerine (örnekler başka yüzeye düşüyor ya da görüntü karşıtlığı > 0,04) 32'lik
  desenin geri kalanı; yeniden kurma süzgeci 1,5 piksel yarıçaplı Blackman-Harris, yoğunluk dengeli (kenar pikselinin
  fazla örneği komşulara fazla ağırlık bindirmez). Doku süzme ışın konisiyle (anizotrop).
- **Tayf:** bütün dalga boyu işi hazırlıkta, 380-780 nm, 5 nm adımla: su (Pope-Fry + Jerlov IB'nin su dışı soğurması,
  Haltrin saçılması), güneş ve gök (G173, Preetham-Perez gök biçimi), göz (CIE 1931). Sonuç, ışığın sudaki yol
  uzunluğuna göre XYZ tablolarına pişer; canlı karede yalnız tablo okunur. Yüzey rengi üç pürüzsüz yansıtma tayfının
  ağırlığıdır (gün ışığında birebir geri döner).
- **Işık alanı (fırın):** 1B su sütununda Monte Carlo (kum dipte, yüzeyden Fresnel ve tam iç yansıma); 81 dalga boyu ×
  300 bin foton, ~1 sn. Güneşin tek saçılması ışık sütunu yürüyüşünde, daha yüksek dereceler tablolarda (çift sayım yok).
- **Yüzey:** Tessendorf FFT (512², 16 m, kendi FFT çekirdeğimiz), frekanslar 24 sn'lik döngüye nicelenmiş; LEAN eğim
  piramidi; alttan Snell penceresi, Fresnel, pırıltı (mikroyüz BTDF, güneş diski + izdüşüm alanındaki eğim varyansı),
  tam iç yansıma aynası (yansıyan ışın izlenir).
- **Kostik hacmi:** her karede 1024² güneş ışını kırılır, logaritmik aralıklı 32 derinlik diliminde (0,5-20 m) çatal
  yayılır, güneş diskinin derinlikle bulanıklığı uygulanır; ışık sütunları aynı hacimden.
- **Işık sütunları:** 2×2 piksel bloğu başına bir kez, 32 sabit uzaklık düğümünde yürünür (3B dokuya); her örnek kendi
  uzaklığını tek süzgeçli okumayla alır. Piksel başına yürüyüşe göre 11 → 1,5 ms.
- **Dünya:** kum (ambientCG Ground054, ×1,5 ton), taranmış kaya (Poly Haven boulder_01, 2,8 m), ~36 bin yapraklık çayır
  (düz kuadratik B-spline şeritler, ölü dalga akıntısıyla salınır, kinematik), kumda dalgacıklar (dünya uzayı normal
  haritası, mip'li). Fırınlanmış ortam kararması (köşe başına 128 ışın), sudaki güneş görünürlüğü hacmi (10 cm).
- **Dalgıç:** ~90 kg + eklenen kütle, ikinci dereceden direnç (CdA 0,25), palet itkisi 32 N (Shift 85 N): seyir
  ~0,5 m/sn. Taban, kaya ve yüzeyin 1 m altı sınır. Maske: düz cam (su altında her şey 4/3 yakın, görüş daralır).

### Plandan ayrılanlar (gerekçesiyle)
- **Su tipi IB** (plan "kristal Ege, I-IA"): Solonenko-Mobley değerleriyle IA'da görüş ~70 m ve parçacık saçılması
  (b(550) = 0,0035) ışık sütunu verecek kadar değil; IB'de görüş ~45 m (planın haritası 30-40 m'de maviye kaybolmayı
  istiyor), sütunlar görünür. `water.jerlov` ile değişir.
- **Kostik için kısa dalgalar 35 cm'de kesildi:** tam tayfla (20 cm'ye kadar) ağ 3 m'den derinde benek gürültüsüne
  dönüyordu (kısa dalgalar sığda odaklanıp derinde kaotik). Korunaklı, sakin bir koyda kısa dalgalar zaten zayıf; eğimleri
  çözülmeyen pürüzlülüğe (pencere ve pırıltılar) aktarıldı. 20 / 35 / 60 cm yan yana denendi, 35 gerçek fotoğraflardaki
  ağa en yakın. Ayrıca hafif bir ölü dalga (8 ve 5 m).
- **Güneş diski bulanıklığı derinlik × 0,0069** (plan 0,0093 yazmıştı: o havadaki açı; su altında 0,53°/1,333).
- **Kenar örneği varsayılanı 16** (plan 16-32): 32 ile kare ~41 fps sınırında; düğmeyle 32.
- **Kumdaki dalgacıklar ağda değil, normal haritasında:** 9 cm'lik dalgacık 3 cm'lik ağ köşelerinde örneklenince geniş
  moiré bantları yaptı (zamanla görünür olup kaybolan karanlık şeritler). Artık dünya uzayında mip'li bir normal haritası,
  ışın konisiyle süzülüyor (yakında keskin, uzakta ortalanır).
- **Zamansal düğmesi yok** (plan: zamansal açık/kapalı): zamansal birikim hiç kurulmadı; yerine durunca birikim (P).

### Ölçümler (RTX 4080, 2560×1440)
- **Kare aralığı 41 fps sabit** (plan §1): sahne `Host::pace(41)` ister, uygulama tazeleme periyodunun 4 katına kadar
  bekleyip dikey eşitlemeyle takas eder (G-Sync 1'den büyük takas aralığını yok saydığı için yazılım sınırlayıcı;
  `timeBeginPeriod(1)`). Otomatik testte 41,2-41,5 fps; panelde "Kare: 41 sabit / serbest".
- **Kare süresi** (başlangıç bakışı, 4 + 16): dünya 5,5 (kostik 5,0) · ışık sütunları 1,5 · örnekler 6,2 · kenar 2-5 ·
  çözüm 1,5 · ton 0,1 → **~17-19 ms, 53-60 fps**. Kenar 32: ~24 ms (41 fps). Örnek 16: ~25 ms.
- **Kararlılık** (yol başına 100 kare 41 fps'te, referans 256 örnek):

  | Yol | Piksel kıpırtısı ort / %99 | 8×8 blok ort / %99 | Bağıl | FLIP |
  |---|---|---|---|---|
  | Koy sabit (kamera durgun, dünya akıyor) | 0,00054 / 0,0036 | 0,00011 / 0,00057 | 0,096 | 0,0058 |
  | Koy yüz (0,5 m/sn ileri) | 0,00076 / 0,0064 | 0,00014 / 0,00076 | 0,119 | 0,0060 |
  | Koy dön (12°/sn) | 0,00079 / 0,0064 | 0,00014 / 0,00078 | 0,092 | 0,0059 |
  | *Fener A (taban)* | *0,0053 / 0,050* | *0,0011 / 0,0125* | *1,24* | *0,038* |
  | *Fener yolculuk (taban)* | *0,0061 / 0,080* | *0,0026 / 0,036* | *0,45* | *0,026* |

  **Hedef tuttu:** her ölçüde Fener'in yarısından iyi; piksel kıpırtısında ~7-10 kat, 8×8 blokta ~8-25 kat, netlikte
  (FLIP) ~4-6 kat. Not: Fener'in kıpırtısının bir kısmı her karede değişen titreşimden (±1/255); Koy'un titreşimi sabit
  desen. Blok ölçüsü titreşimi ortaladığı için en adil kıyas o.

- **Fizik testleri** (`--test`): Snell penceresi 48,19° (beklenen 48,61 ± 0,5) GEÇTİ · su ve Fournier-Forand faz
  integralleri 1 · kesik fazın geri saçılım payı beklenenle aynı · ışık alanının derindeki Kd'si (10-35 m) yayımlanan
  IB'ye %2-12 yakın (sınır %15; mavide fark büyük: Kirk ilişkisi öfotik katman ortalaması, mavide o katman ~70 m) ·
  kostik her dilimde enerjiyi korur (ortalama 1 ± 0,02).
- Başvuru panosundaki ölçü: gerçek fotoğraflarda kumdaki kostik p99/p50 ≈ 2,6-3,6 ("neon ağ değil"); bizimki henüz
  ölçülmedi (Durak 2'nin işi).

## NE DOĞRULANMADI
- **Canlı görünüm Özcan'ın gözüyle:** her şey komut satırı kareleri ve otomatik testle; Durak 1 bunun için.
- Kararlılık yalnız üç yolda ölçüldü; çayırın içinden yüzme, yukarı (pencereye) bakarak dönme ölçülmedi.
- Görüş testi (siyah hedef, 4,8/alfa) ve kostik bulanıklığı testi yazılmadı; kabarcık, yüzerlik, Bühlmann testleri K3.
- Çayır yapraklarında ışık geçirgenliği yok (arkadan aydınlanan yaprak koyu kalıyor); uzak çayırın yoğunluk örtüsüne
  geçişi (plan §5.1) yok: Durak 1'de çayırın kendisi en zor kararlılık sınavı olarak duruyor.
- Fırınlanmış ışık sondaları (DDGI) yok: dolaylı ışık 1B ışık alanı + ortam kararması. Kayanın kuma yansıyan ışığı yok.
- Gölgeler güneşin ortalama kırılma yönünde tek ışınla (sert kenar); dalgaların gölgeyi oynatması yok.
- `--autotest-only koy` çalıştı (yükleme, yüzme, bakış, panel, durdurma), ekran görüntüleri `captures/arayuz/koy_*`.
