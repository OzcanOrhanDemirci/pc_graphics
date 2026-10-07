# DURUM · Sahne 10 "Kuyu"

> Otonom oturum, 2026-10-07 öğleden sonra (plan `docs/kuyu/PLAN.md`). Plandan sapmalar aşağıda "Kararlar"da,
> gerekçesiyle.

## Özcan: nasıl açılır, ne denenir

- `build\pc_graphics.exe` → galeride **10 Kuyu** (galeride **0** tuşu ya da aşağı ok) → Enter.
- Görünmez bir gözlemcisin, iki oda sütunu arasındaki kuyudan saniyede 1,5 m düşüyorsun.
- Fare tıklamadan bakar (Denizaltı'nın sözleşmesi; **Tab** imleci bırakır). **W A S D** yatay kayma (odalara ve
  demetlere çarparsın), **Boşluk / C** yüksel / alçal, **Shift** basılı tutuldukça gittikçe hızlanır (aşağıda). **Q / E** düşüşü yavaşlatır / hızlandırır (dur,
  0,5, 1,5, 4, 10 m/s). **1 2 3 4** bestelenmiş bakışlar: kuyu, oda, kafes, yukarı (**2** en yakın ışıklı odanın
  önüne getirir ve düşüşü durdurur; **E** sürdürür). **R** başa döner, **↑ ↓** pozlama. Ayar paneli yok: kalite
  2560 × 1440 için ayarlı.
- Denenecekler: (1) başta dosdoğru aşağı bak: tek kaçış noktası, kat kat geçen odalar; (2) **2** ile bir odanın önünde
  dur: lambanın görüntüleri içeride üç boyutlu bir kafeste sonsuza gider, derinde kehribara döner; (3) **Q** ile dur ve
  fareyi yavaşça gezdir: yıldız alanları titremeden kaymalı (titriyorsa söyle: ölçüm aşağıda); (4) **W** ile bir odanın
  camına yaklaş; (5) **E E** ile hızlan.

## Revize 1 (Özcan, 2026-10-07 akşam): Shift ile sonsuzlukta hızlanmak

Özcan: *"Test ettim çok güzel olmuş. Çok sonsuz hissettiriyor. [...] shifte basılı tutarken gittikçe hızlanan şekilde
[...] önce yavaş sonra çok hızlanmak. En son noktada aşırı hızlı. Hıza özel hiçbirşey yok. Sadece hızlı hareket
ediyorum."* → Shift basılıyken hareketin (kayma ve düşüş birlikte) hızı üstel büyür: her `boost.tau` = 1,3 sn'de e katı,
tavan `boost.max` = 120 m/s (1,5 m/s düşüşten tavana ~5,7 sn); bırakınca 2,5 kat hızla geri iner (yarılanma ~0,4 sn).
Hıza bağlı hiçbir görüntü etkisi yok; panelde yalnız "Hız" ölçümü. **Tavan neden 120 m/s:** kafes 8 m'de bir yinelenir;
kare başına yarım hücreden (33 FPS'te ~130 m/s) fazla ilerleyince göz hareketi izleyemez, kafes durmuş ya da geri
gidiyormuş gibi görünür (araba tekerleği etkisi). İstenirse `boost.max` ile kaldırılır. Otomatik test: Shift ile 3 sn'de
36 m/s, 7 sn'de 120 m/s, kare süresi değişmiyor (33 FPS); bırakınca 3 sn'de 4 m/s. Uygulamada `in.shift` artık otomatik
testin basılı tuttuğu sanal tuşları da görüyor (gerçek tuş için davranış aynı).

## Bir bakışta

- **Kafes:** hücre 8 m, her hücrenin ortasında 3,6 m'lik bir oda: yarım inç gri cam, iç yüzünde 45 nm gümüş film. Odanın
  12 kenarı sonsuza uzanan **demetler** (0,6 m): her eksende oda sırası başına dört çizgi. Üç aile birbirine değmez: a
  ekseni boyunca uzanan demet a+1 ekseninde odanın dışında, a+2'de odanın açıklığının içinde durur (her köşede bir
  fırıldak); her yüzün iki karşı kenarında birer demet yatar.
- **Kuyu:** ayrı bir boş sütun gerekmedi: demetlerin arasında kendiliğinden sonsuz dikey kanallar kalıyor. Düştüğümüz
  kanal iki oda sütununun arası: iki yanda 2,2 m ötede odalar geçer.
- **Demetler:** çıta başına 5 iplik (kitap sırtları: kemik, ceviz, siyah lake, gri bez, ten deri, bordo, zeytin, altın
  varak), çıtalar 0-3 cm kabartmalı. Cilalı iplikler Fresnel'le (n 1,5), altın varak altının ölçülmüş sabitleriyle
  yansıtır; uzaktan iplikler izdüşüme göre ortalanır.
- **Odalar:** 20 tür (yıldızlar: farklı yüksekliklere asılı 3-5 ampul; tek lamba; ay ışığı 5200-6500 K; krom küre;
  bakır sarkaç; 45° ayna; köşe yansıtıcı; lamba sırası; mum 1900 K; karanlık), her oda küpün 48 simetrisinden biriyle
  döndürülmüş. Işıklar çoğunlukla 3300-4300 K. Odanın kenarlarında krom bir çerçeve: aynalarda sonsuza tekrarlanan
  dikişler.
- **Hız:** canlı uygulamada (otomatik test, dört bakış, düşüş, kayma, hızlı düşüş) **33-42 FPS**, kart belleği 1,9 GB.
  Başsız kare 19-31 ms.

## Fizik ne derse o: değerler ve kaynakları

| | Değer (610 / 550 / 465 nm) | Kaynak |
| --- | --- | --- |
| Gümüş film | n 0,058 / 0,060 / 0,047, k 4,09 / 3,60 / 2,80; 45 nm | P. B. Johnson, R. W. Christy, *Phys. Rev. B* 6, 4370 (1972) |
| Gri cam | 12,7 mm, görünür geçirgenlik %18 (iç geçirgenlik 0,196) | GGI "Tinted Float Glass" veri sayfası (gri 1/2") |
| Cam kırılma | 1,5225 / 1,5251 / 1,5307 | M. Rubin, *Sol. Energy Mater.* 12, 275 (1985) |
| Yansıma önleyici | %0,5 dik gelişte (çok katmanlı), açıya göre camın Fresnel biçimiyle | seçim |
| Krom | n 3,19 / 2,79 / 2,14, k 4,27 / 4,20 / 3,93 | A. D. Rakić vd., *Appl. Opt.* 37, 5271 (1998) |
| Bakır | n 0,378 / 1,007 / 1,247, k 3,11 / 2,58 / 2,46 | Johnson ve Christy (1972) |
| Altın | n 0,226 / 0,424 / 1,332, k 3,19 / 2,47 / 1,87 | Johnson ve Christy (1972) |

- **Duvar** (dik geliş, 550 nm): içeriden **%94,5** yansıtır (kırmızı %95,8, mavi %90,9), dışarıdan **%4,1**, iki yönde
  **%0,71** geçirir. Tek yönlülük yalnız aydınlık içeriyle karanlık dışarının farkı (geçirgenlik iki yönde aynı:
  karşılıklılık). İnce gümüş maviyi daha çok geçirir, kırmızıyı daha çok yansıtır: derin yansımalar kehribara kayar.
- **Sarkaç:** T = 2π√(l/g)·(1 + θ₀²/16), l = 3,1 m, θ₀ = 0,32 rad: 3,6 s.
- **Mum:** 1900 K, 0,25 × tek lamba şiddeti, 1,2 cm.

## Nasıl çiziliyor (mimari)

- **Analitik kafes izleyici (CUDA).** Işın hücreden hücreye 3B yürür (DDA), her hücrede oda kutusu ve 12 demet (kare
  prizma). Demet yüzündeki çıtaların kabartması kesin yürünür (eksen boyunca sabit bir yükseklik alanı).
- **Cama çarpan ışın** duvarın yaptığını yapar: dış yansıması (%4) dışarıda sürer (çarptığı odada odanın ortalama
  parıltısını görür), geri kalanı yarım inçlik camdan kırılarak geçer ve odanın içinde gümüş duvarlar, aynalar ve metal
  arasında seker. Ampuller ışının izdüşümüyle: büyükken disk örtüşmesi, pikselden küçükken (derin görüntü) eşit alanlı
  bir Gauss.
- **Oda içi kesme: görünmezlik sınırı.** Kalan yolun herhangi bir ampulden ekrana katabileceği en çok ışık w·I/(π·R²)
  (R ayak izi, yalnız büyür) pozlanmış görüntüde AgX'in en dik yerinde yarım kod değerinin altına inince yol biter.
- **Pikselin örnekleri bir odayı paylaşır:** 6 sabit örnek dışarıyı (kenarlar) izler; aynı oda yüzüne girenlerin içi,
  pikselin merkezine en yakın örnekten tek ışınla (izdüşümü, paylaşan örnek sayısının dördüncü kökü kadar genişler), küre ya da serbest
  ayna olan odalarda (içeride sert kenar) üç ışınla.
- **Yapının ışığı:** demetleri yalnız odaların camından sızan ışık aydınlatır. Kafes yinelendiği için bir hücrenin demet
  yüzleri üzerinde bir kez tablo kurulur (2 cm × 2,5 cm texel, 460 bin texel, ~0,3 s): texel başına 4096 kosinüs ağırlıklı
  ışın (Fibonacci sarmalı, rastgele sayı yok), her ışın çarptığı oda yüzüne yazılır (27 komşu odanın yüzleri ayrı, en
  büyük 16'sı; ötesi ve pus ayrı). Odalar yinelenmez: her oda yüzünün parıltısı (oda türünün o yüzden çıkan ortalaması ×
  ortak açısal biçim) kare çizilirken çarpılır.
- **Pus:** σ_s 0,006 /m, σ_a 0,001 /m; çevresinin ortalama parıltısıyla (odaların) kapalı biçimde. Kuyunun sonunda
  geçirgenlik 1200 m'de 1e-4: görüş pusta söner.
- **Görüntü:** pikselde 6 sabit örnek (kale deseni) + `recon` 0,12, AgX, parlama; birikim ve gürültü giderici yok.
  Oda türleri, yüz parlaklıkları ve simetri tablosu blok başında paylaşımlı bellekte.

## Ölçümler (Ö1-Ö7)

| # | Ölçüm | Hedef | Sonuç |
| --- | --- | --- | --- |
| Ö1 | Bir odada lambanın n'inci görüntüsü analitik yerinde (görüntü kafesi x_a = 2ah + (−1)^a x, camın yan kayması kapalı biçimde), sahnenin kendi oda koduyla | < 0,1 px, n ≤ 30 | **0,00013 px** (16 görüntü, n 1-30, 1440p). `--test o1` |
| Ö2 | Duvar tabloları ↔ bağımsız hesap (Abelès matrisi, Macleod'un işaret kuralında); R + T + A = 1; T iki yönde aynı; içeri/dışarı | ±0,005 | **< 1e-6** her tablo; soğurma ≥ 0; karşılıklılık 2e-15; içeriden %94,5 / dışarıdan %4,1 (oran 23). `--test o2` + `python tools/kuyu_o2.py` |
| Ö3 | Kararlılık, `tools/kararlilik.py` (referans 16 örnek × 4 kayma, her örnek kendi oda içiyle) ve `tools/titreme.py` (süzgeçten bağımsız zamansal ikinci fark) | Ayna düzeyi (~0,0014-0,002) | **Yavaş kayma** (kare başına ~0,25 px; titremenin görüneceği yer): kıpırtı **0,0028**, titreme oranı **1,06** (canlı, 64 örnekli referans kadar sakin). **Hızlı düşüş** (1,5 m/s): kıpırtı 0,0099, titreme oranı 1,01. Tutmadı (mutlak), aşağıda. `--test o3` (`--set o3.path=2` yavaş) |
| Ö4 | Yapının ışığı: tablo ↔ kaba kuvvet (yarım küreye 1 milyon ışın, odaların içi tam derinlikte) | ≥ 0,99 | Tablonun makinesi (aynı yayıcı modelle): **0,989**. Fiziğe karşı: **0,935, tutmadı** (aşağıda). `--test o4` |
| Ö5 | 2560 × 1440, büyütmesiz | 30-40 FPS | Canlı **33-42 FPS** (en ağırı kuyu bakışı ~29 ms). Başsız 19-31 ms |
| Ö6 | Bozuk piksel, rastgele poz, belirlilik | 0 / temiz / aynı | 1000 poz (640×360) ve 150 poz (1440p) temiz, bozuk piksel 0; dört bakış üç ayrı süreçte bit bit aynı (md5). `--test fuzz` |
| Ö7 | Sonsuzluk kesilmez | < 1/255 | Kesmeli ↔ kesmesiz (sınır 1e-9): en çok 2 kod değeri, piksellerin %0,0001'inde > 1. Kuyunun sonu pusta (1200 m'de geçirgenlik 1e-4), kara duvar yok |
| | Çarpışma | | 263 yürüyüş × 400 adım (5 cm): engele hiç girmiyor, boştaki nokta itilmiyor. `--test carpisma` |

**Ö3 neden tutmadı:** kıpırtı ölçüsü canlının değişimini referansınkiyle kıyaslıyor; hareketli ince ayrıntıda canlı
referanstan farklı bir süzgeçle çizilmişse, hiç titremese de "fazla değişim" sayılıyor. Hızlı düşüşte yıldız alanları
kare başına onlarca piksel kayıyor; tek ortak oda içi ışını ile 6 ayrı ışının farkı burada büyük (6 ayrı ışın 0,0039,
ama kare 33-44 ms). Süzgeçten bağımsız ölçü (zamansal ikinci fark) canlıyı referans kadar sakin buluyor (oran
1,01-1,06). Titremenin asıl görüneceği yavaş kaymada kıpırtı 0,0028.

**Ö4 neden tutmadı:** tablo bir oda yüzünü tekdüze bir yayıcı sayar. Gerçekte camdan çıkan ışık yüze yayılmış değil:
ampullerin kendisi ve ilk görüntüleri camın bir yerini daha parlak yapar; cama 3 cm'den bakan demet yüzlerinde fark
%10-28. Oda türünün yöne bağlı parıltısını kullanmak düzeltmedi (0,93: hata açısal değil mekânsal). Düzeltmek her
gölgelendirmede ampul başına gölge ışını ister (27 odayı piksel başına hesaplamak 70 ms tutuyordu). Görünür etkisi:
ampule yakın demetlerde yumuşak parlak lekeler yok; toplam ışık seviyesi %1 içinde doğru (tablo / kaba kuvvet 1,009).
Denendi: demetin üzerinde yattığı odanın ampullerinin doğrudan ışığını her gölgelendirmede kesin hesaplamak (noktasal
ışık, camdan geçiş, içerideki küre ve aynaların, dışarıdaki demetlerin örtmesi; tablo o oda için "doğrudan hariç"
parıltıyı kullanır). Cama yakın noktalarda hata %25-28'den %4-5'e indi, toplam uyum 0,935 → 0,941 (kalan hata komşu
odaların ampulleri); görüntüde etkisi piksellerin en çok %0,08'inde 3 kod değeri, bedeli kare başına 6-9 ms. Kapalı
(`light.own.direct = 1` açar; kapalıyken görüntü bit bit önceki).

## Kararlar ve plandan sapmalar

1. **Demetler odaların kenarlarından çıkar** (planda hücre kenarlarındaydı): tesseract'ın mantığı (çizgiler odanın
   kendi uzantısı), daha yoğun kafes, kuyu kendiliğinden.
2. **Dipteki soğuk ışık yok:** sonsuz kafeste sonsuzdaki bir ışık hiçbir yere ulaşamaz (her doğrultu bir odaya ya da
   demete çarpar, yalnız eksenler boyunca açık kanallar kalır); bizimle düşen sahte bir ışık olurdu. Soğuk vurgular ay
   ışıklı odalardan ve ince gümüşün maviyi geçirmesinden.
3. **Pusta oda hâleleri yok:** σ_s 0,006 /m'de pencerenin 1 m yakınından geçen 4 m'lik yolda saçılan ışık pencerenin
   ~%0,6'sı: görünmez. Parıltıyı lens (bloom) yapar.
4. **Oda içi kesme görünmezlik sınırıyla.** "Girişin 1/255'i" kuralı görünür fark veriyordu (oda bakışında piksellerin
   %28'i 2 koddan fazla): ampuller çok parlak, derin görüntüleri de görünüyor. Kesme pozlamaya bağlı (↑ ↓ ile derinleşir).
5. **Kuyruk denendi, bırakıldı:** ilk N sekmeden sonrasını odanın o yöndeki ortalama ışınımıyla kapatmak noktaların
   arasındaki karanlığı turuncu bir sisle dolduruyordu (gerçekte derin görüntüler seyrek noktalar). `tail = 1` açar.
6. **Bir pikselin aynı oda yüzüne giren örnekleri oda içini paylaşır** (oda bakışında 187 → 25 ms). Her örnek kendi
   ışınıyla (`interior.reps = 0`) daha kararlı ama 33-44 ms.
7. **Kutu izdüşüm denendi, bırakıldı:** ortak ışını pikselin merkezinden, izdüşümü pikselin karesi (aynalarda eksenleri
   taşınarak, diskle karenin kesin örtüşmesi) yapmak titremeyi azaltmadı, artırdı (yavaş yolda oran 1,55).
8. **Yapının ışığı ışınlarla:** önce her oda yüzü için kesin izdüşüm katı × 8×8 gölge ışını denendi; yakın pencerelerde
   cos·cos/r² ağırlığı çok sivri, ızgara kaçırıyordu (makine uyumu 0,93). Texel başına kosinüs ağırlıklı ışın demeti
   aynı integrali yakın ve uzak odalar için tek döngüde veriyor (0,989).
9. **Paylaşımlı bellek ve 32 bitlik karma:** global bellekten okunan oda → tür → parlaklık zinciri kareyi 40 ms'ye
   çıkarıyordu (paylaşımlı bellekle 14). 64 bitlik çarpma kartta yavaş: hücre dizinleri 64 bit, karma 32 bit.
10. **Referanslardan (Özcan'ın üç tesseract karesi):** çizgiler ince ve çok, aralarında parlak ve koyu iplikler, çoğu
    ışığı yansıtıyor, renk nötr-sıcak. İlk turdaki turuncu-kahve tek renklilik bu yüzden bırakıldı: ışıklar 3300-4300 K,
    çerçeve krom (bakır aynalarda sonsuza tekrarlanınca her şeyi turuncuya boyuyordu), iplikler daha nötr.
11. **Kendi odanın ampulleri kesin: denendi, kapalı** (Ö4'ün altında): fiziği iyileştiriyor, görünmüyor, 6-9 ms.

## Gece boyunca bulunan ve düzeltilen hatalar (kısa)

- Tablonun gölge ışınları yüzeyden 0,2 mm kaldırılmış noktadan atılıp uzaklık kaldırılmamış noktadan ölçülüyordu:
  sıyırarak giden ışınlarda fark 2 cm'yi aşıyor, ışın hedef camın kendisine çarpıyordu (cama yakın demet yüzleri
  karanlık kalıyordu).
- Tablonun pencereleri demetlerin altındaki cam şeridini dışarıda bırakıyordu; o cam ışığını tam da demetin alt yüzüne
  verir (örtmeyi gölge ışınları zaten hesaplıyor).
- Ö2'nin bağımsız aracı Abelès matrisine n + ik veriyordu (matris Macleod'un n − ik kuralında): film "kazançlı" çıkıyordu.
- Galeri yalnız 1-9 tuşlarını tanıyordu: 10. sahne için 0.

## NE DOĞRULANMADI

- **Canlı kontroller bir insan elinde denenmedi**: fare ve A/D yönü sözleşmeye göre (görüntünün sağı = render'ın sağ
  vektörü, yalnız sapmadan: dosdoğru aşağı bakarken de tanımlı), otomatik test sanal tuşlarla geçti. Ters kontrol iki
  kez kaçmıştı: ilk denemede bak.
- Çarpışmanın hissi (demetlere sürtünme, oda camında kayma) denenmedi; yalnız ölçüldü.
- Ö3 ve Ö4'ün mutlak hedefleri tutmadı (yukarıda).
- Hızlı düşüşte (4-10 m/s) oda içlerinde ince kıpırtı olabilir (ölçünün en yüksek olduğu yer); 1,5 m/s ve durgunda ölçüldü.
- Odalara giren dış ışık (yapının ve komşu odaların ışığının bir odanın içinde sekmesi) yok: T² ≈ 5e-5, ihmal edildi.
- Demetler arası ışık (bir demetten yansıyıp ötekine düşen) yok; cilalı ipliklerin yansıması var.
- Pus yalnız ortalama çevre parıltısıyla; odaların hemen önündeki pus aynı yoğunlukta.
- Camın dış yansıması odanın ortalama parıltısını gösterir, ayrıntısını değil (%4'ün içinde).
- Çerçevenin dış yüzü koyu yayıcı sayıldı (kromun dışarıdaki yansıması izlenmedi); ışığı 27 odadan, gölgesiz.
- Sarkaç odalarının ışık tablosu sarkaç durağanken kuruldu (yapının ışığında sarkacın etkisi yok).
- Ses yok. Odanın içine girmek ve odalar arası zaman farkı (planın ikinci oturumu) yapılmadı.

## Araçlar

```bat
build\pc_graphics.exe --scene kuyu --headless --out captures\kuyu [--view 0..3] [--cam "x y z yaw pitch fov"] [--time s] [--probe "x y"] [--debug 1|2]
build\pc_graphics.exe --scene kuyu --test o1|o2|o3|o4|fuzz|carpisma
python tools\kuyu_o2.py captures\kuyu\o2\o2_tablolar.csv
python tools\kararlilik.py captures\kuyu\o3   ·   python tools\titreme.py captures\kuyu\o3
build\pc_graphics.exe --autotest-only kuyu
```

Kod: `src/scenes/kuyu/` (`kuyu_geom.h` kafes, demetler, karmalar, simetriler; `kuyu_kernels.*` izleyici, oda içi, ışık
tablosu, araç çekirdekleri; `kuyu_app.*` optik tablolar, oda türleri, çarpışma, araçlar; `kuyu_live.cpp` sahne).
Ayarlar `scenes/kuyu.cfg`.

## Sıradaki

Özcan'ın denemesi. Konuşulursa: odanın içine girmek (Inception'ın bir kat aşağısı), odalar arası zaman farkı, ampule
yakın demetlerdeki parlak lekeler (Ö4).
