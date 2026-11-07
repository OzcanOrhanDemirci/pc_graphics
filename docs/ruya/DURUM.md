# Rüya · Sahne 14

`build\pc_graphics.exe --scene ruya` sahneyi açar. Galeride **14 Rüya**.
**1–6** genel manzarayı, havuzu, kaya yüzünü, kuleyi, koruluğu ve doğal
kemeri gösterir. WASD ve fare uçuşu yönetir. Boşluk ve C yükselme ve
alçalma, Shift hızlı uçuş, Tab imleci bırakma ve alma içindir. Fare dönüşü
0,85 rad/s ile sınırlı. Dikey görüş açısı tekerlekle 36°–48° arasında değişir.

## Dünya ve görüntü

Koordinat dört bölge tarifinden birini seçer. Yirmi yedi bileşen geometrisi
varsayılan ayrıntıda toplam **2.382.774 üçgen** içerir. Arazi, yapı ve ağaç
ayrı yerleşir. Çekirdek kaya kemerleri, ince sahanlıklar, koruluklar ve
ayrık taş grupları silueti değiştirir. Bazı koordinatlar açık uçuş koridoru
bırakır. 32×32 koordinat örneğinde 250 hücre boş, 774 hücre dolu. Konum
ve açıyı saymadan 502 ayrı bileşen sayımı bulundu. Tekrar üretimde
yerleşim baytları eşleşti. Galeri görseli sahnenin kendi çizdiği karedir.

Kamera 27 cm yarıçaplı bir küredir. Hareketin tamamını çizilen üçgenlere
karşı tarar. Temastan sonra yüzey boyunca kayabilir. Aynı geometrinin CPU
ağaçları bellekte durur. Hücreye özel çarpışma yerleşimleri hareket
sorgusu sırasında üretilir.

Kamera, görüş konisini kesebilecek hücreleri koordinatlarından seçer.
Örtülme hesabından önce kullanılan konservatif sınır bütün adayı kapsar.
Kamera hareket ettiğinde hücrenin biçimi, tohumu, ölçeği ve dönüşü değişmez.
Bu seçim için ayrıntı seviyesi geçişi kullanılmaz.

Işığın kadraj dışına ulaşması için yaklaşık 1,1 km yarıçaplı aday çevrenin
ortak ağları kısa ömürlü bir OptiX tablosunda yer alır. Kamera ışınlarının
erişimi ayrı bir maskeyle sınırlıdır. Bu kararla “bakılan dünya” tanımına
görüntüye ışık taşıyabilecek çevre de girer. Çevre adaylarının geçici GPU
verisi vardır. GPU ışın izlemeyi bitirince hücre kayıtları, yerleşimler,
hızlandırma tablosu ve çalışma tamponu sıfırlanır. Sıfırlanmış tampon
kapasitesi yeniden kullanılır. Kalıp ağları ve görüntü tamponları sahne
kapanana kadar bellektedir. Diskte dünya durumu tutulmaz.

Varsayılan 2560×1440 çizim üç sabit kamera örneği, üç yüzey sekmesi,
son gök ışını, güneş diski örneklemesi ve MIS kullanır. Lambert ve GGX
arasındaki örnek dağılımı malzemenin yansıtma oranına göre değişir. Taş
için ton, pürüzlülük ve iki kabartma eğimi açılışta GPU'da 4096×4096
olarak üretilir. Pikselin yüzeydeki izdüşümüne göre 13 mip seviyesi
arasında süzülür. OptiX HDR gürültü gidericisi kare geçmişi taşımaz.
Taş renginin bir bölümü gürültü giderme sonrasında yeniden uygulanır.
Gök ve sis yaklaşık atmosfer modeli kullanır. Bulut örtüleri birincil
gök ışınlarında analitik katmanlardır. Havuz opak yansıtıcı yüzeydir.
[NVIDIA OptiX 9.1 kılavuzu](https://raytracing-docs.nvidia.com/optix9/guide/index.html)

## Ölçüm düzeni

RTX 4080, sürücü 621.01, 16.376 MiB. Kayıtlarda CUDA olay süreleri ve
CPU duvar saati ayrı. Toplam kare süresine görünür hücre seçimi, geçici
ışık tablosu, çizim ve gürültü giderme dahil. Monitör sunum süresi ayrı
bir ölçümdür. Ölçüm sırasında pencere veya ses açılmadı.

```powershell
cmd /c build.bat
.\build\pc_graphics.exe --scene ruya --test-live
python tools/ruya_verify.py --self-test
python tools/ruya_verify.py --mode all --frames 200 --warmup 35 --repeats 2 --reference-spp 256 --temporal-spp 512
.\build\pc_graphics.exe --scene ruya --headless --view 2 --impact --out captures/ruya/temas
.\build\pc_graphics.exe --scene ruya --headless --view 2 --impact --flat-material --out captures/ruya/temas_haritasiz
python tools/ruya_verify.py --mode temporal --temporal-view 2 --temporal-frames 20 --temporal-width 2560 --temporal-height 1440 --temporal-spp 256
```

Python ölçümleri NumPy, Pillow ve OpenEXR kullanır. FLIP kuruluysa görüntü
farkı ayrıca kaydedilir. Her koşu komutu, çalıştırılabilir dosyanın karması,
kaynak karmaları, GPU bilgisi, ham kare süreleri ve çıkış kodu manifestte.
Başarı eşiği tutmazsa araç çıkış kodu 1 verir ve verileri saklar.

## Uçuş geri bildirimi sonrası ölçüm · 2026-11-07

`captures/ruya/revision_final/` altı sabit bakışın iki tekrarını ve
200 karelik uçuşu içerir. Toplam **2.600 native 1440p kare** ve
**0 sonlu olmayan örnek**. Yol izleme, kamera ve ışık tabloları,
gürültü giderme ile son renk dönüşümü süreye dahildir.

| Ölçüm | p50 | p95 |
| --- | ---: | ---: |
| En ağır sabit bakış, iki tekrar | 30,22–30,29 ms | 30,90–32,01 ms |
| Uçuş, 200 kare | 23,80 ms | 26,29 ms |
| Gizli CUDA–OpenGL, 120 kare | 28,86 ms | 30,13 ms |
| İlk sürüm, aynı 180 karelik girdi | 31,78 ms | 35,73 ms |
| Yeni sürüm, aynı 180 karelik girdi | 24,31 ms | 26,64 ms |

İki sürüm aynı başlangıç pozu, girdi, çözünürlük ve örnek sayısını
kullandı. Dünya üretimi değiştiği için GPU'ya verilen geometri farklı.
Bu iki koşu `revision_baseline/route.csv` ve
`revision_final/route_equal_180.csv` içinde. Gizli CUDA–OpenGL turu
çarpışma hesabını ve görüntü aktarımını da içerir. Gerçek monitör
sunumu ayrıca ölçülmedi.

960×540 başlangıç uçuşunda 512 örneğe göre fazla piksel değişimi
**0,00535** ve ikinci fark oranı **0,984**. 1440p yakın kaya uçuşunda
256 örneğe göre **0,01105** ve **0,985**. Yakın uçuşun 0,006 eşiği
açık. Aynı 48 karelik 960×540 yakın yolda süzgeçsiz 4K okuma 0,01411
fazla değişim üretti. Filtreli yol aynı referansa göre 0,01079.
İki ayrı 512 örnekli referans aynı yakın yolda birbirine göre 0,00692
fark verdi. Bu değerler kabul eşiğini değiştirmiyor.

1440p temas deneyinde GPU merkez ışını başlangıç yüzeyini 18,56 m'de
gördü. 19,21 m ileri komutunda kamera 18,15 m hareket etti. Son kadrajda
görülen en yakın yüzey 0,629 m. İnce duvar deneyi 27 cm temas yarıçapını,
duvar boyunca kaymayı ve açık boşluktan geçişi ayrı sınadı. Duvar
çıkarılınca aynı hareket hedefe ulaştı. Altı sabit görüntüde 256 örnekli
ham referansa göre sRGB ortalama fark 0,00290–0,00831. Işık maskesi
kontrolleri ve 20 boşaltıp yeniden üretme çevrimi geçti.

Dört örnekli aday yakın sabit bakışta 34 ms üstüne çıktı. Taşın ağırlığına
göre ışın seçen üç örnekli aday hem kare süresinde hem aynı yakın
rotadaki hareket ölçüsünde öne geçti. Ayrı A/B/B/A denemesinde erken
ışın sonlandırma ve son sekmeyi gölge sorgusuna indirme birlikte yaklaşık
3,5 ms daha yavaştı. Bu değişiklikler üretim sürümüne alınmadı.

## İlk sürümün ölçümü · 2026-11-07

369312a sürümünün ölçümü `captures/ruya/final_stratified/`.
Altı sabit kadraj, kadraj başına üç ayrı süreçte 300 kare ve 300 karelik
uçuş rotası toplam **5.700 native 1440p kare**. Kaydedilen sonlu olmayan
örnek sayısı **0**.

| RTX 4080 ölçümü | Sonuç |
| --- | --- |
| Sabit kadrajların medyanı | 26,86–33,38 ms, yaklaşık 30–37 FPS karşılığı |
| Uçuş rotası | p50 29,39 ms, p95 34,11 ms, en uzun 34,79 ms |
| En ağır yakın bakış, üç tekrar | p50 33,35 / 33,30 / 33,38 ms |
| Başlangıç kadrajı, bağımsız süreç duvar saati | 27,43 ve 27,94 ms/kare, 301 ile 1 karenin farkı |
| En yoğun ölçülen kare | 211 kamera hücresi, 105.496.621 yerleştirilmiş üçgen |
| Uygulamanın en yüksek GPU tahsisi | 1.481.931.076 bayt, yaklaşık 1,38 GiB |

33,33 ms p50 iç sınırı en ağır yakın bakışın iki tekrarında yaklaşık
0,02–0,05 ms aşıldı. Uçuş p95 40 ms sınırının içinde. Süreler ısınma,
dosyaya kare yazma ve monitör sunumunu dışarıda tutar. Ayrı süreç duvar
saati karşılaştırmasının ham kaydı `external_wall.json`. Kartın toplam
kullanımı bu kontrolün öncesinde 615, sonrasında 614 MiB ölçüldü.

Altı kadrajın 640×360, 32 örnekli ham HDR çıktısı kamera maskesi
kaldırılan aynı çevreyle **birebir eşleşti**. Işığa görüş dışı adayları
kapatan negatif kontrolde altı çıktı da değişti. NRMSE 0,0194–0,7676.
İlk kadrajın ayrı süreçte tekrarı aynı EXR karmasını verdi. Referans aynı
çevre yarıçapını ve aynı yüzey integratörünü kullanır.

1280×720 sabit görüntü testinde 128 örnekli ham referansa göre sRGB MAE
0,00568–0,01376, FLIP 0,02853–0,05306. 960×540, 48 karelik uçuş dizisinde
512 örnekli referansa göre fazla piksel değişimi **0,01163**. Önceden
konan 0,006 hedefi açık. İkinci fark oranı 0,972 ile 1,10 sınırını geçti.
İki bağımsız 512 örnek dizisi arasındaki farkın 0,00619 olması bu hedefi
yeniden tanımlamıyor. Hareket metriği örnek gürültüsünün yanında mekânsal
gürültü gidericinin ayrıntı farkını da ölçüyor.

Gizli OpenGL sınaması aynı son ikiliyle geçti. Canlı sahnenin ürettiği
doku, renderer'ın CPU'ya indirilen karesiyle bayt düzeyinde eşleşti.
`live_hidden_final.txt` ve GPU boşaltma kontrolü `smoke.txt` içinde.

## Deneyler ve seçimler

- İlk taslakta görüş dışı adalar elipsoit vekillerdi. A/B/A ölçümünde bu
  sorguyu kapatmak, iki 1440p bakışta izleme süresini 6,5–11,6 ms azalttı
  ve ışığı değiştirdi. Aynı deneyde üçgen kopyalarını yaklaşık 90 milyondan
  50 milyona indirmek yalnızca 0,2–0,3 ms kazandırdı. İlk ölçümler
  `captures/ruya/ablation_initial/` içinde.
- Her ışına ayrı ayrıntılı ada sorgusu kuran DDA denemesi iki bakışta
  54,85 ve 108,22 ms sürdü. Yakın bakışta tam çevreyle ham HDR farkı
  0,01934 NRMSE ölçüldü. Frustum çıkışından başlayan aday aynı çıktıyı
  verdi ve daha yavaş çalıştı. Bu yollar geçici OptiX tablosuyla değiştirildi.
  Deneyler `captures/ruya/exact_optimization/` içinde.
- Üç örnekli, aynı sahne ve kamerada SER 41,1–51,9 ms ölçüldü. O deneyin
  eş koşul tabanı 29,6–35,6 ms idi. Hızlı matematik 27,4–33,3 ms ölçüldü.
  Ham HDR'de NRMSE 0,0086–0,0249, görüntülenen sRGB'de ortalama fark
  0,000098–0,000198 idi. Hızlı matematik seçildi. Ham HDR farkı kayda geçti.
  AOV ve HDR gidericinin süreleri yakındı. AOV dünya normali kılavuzuyla
  seçildi. Kayıtlar `captures/ruya/shader_trial/` ve `quality_trial/` içinde.
- Aynı 48 karede ham 64 örnekli referansın 512 örnekliye göre fazla piksel
  değişimi 0,01133. İki bağımsız 512 örnekli referans arasında 0,00619.
  512 örnekli aynı referansa karşı ilk üç örnek 0,01271, merkezlenmiş
  üçlü 0,01185, merkezlenmiş ve ilk sekmesi katmanlanmış üçlü 0,01163.
  Yakın bakışın 120 karelik A/B/B/A turunda p50 33,14 / 33,10 / 33,01 /
  33,13 ms. Son desen seçildi. `sample_trial/` ve `strata_trial/`.

## Doğrulama ve açık sınırlar

- Dünya testleri kalıp geometrisinin sonlu değerlerini, normallerini,
  üçgen alanını, sınırlarını ve bayt düzeyinde yeniden üretimini sınar.
  Kamera seçiminden bağımsız köşe izdüşümü ve negatif kontroller var.
- GPU yaşam döngüsü sınaması boşaltılan dünyada ilk ışınların geometriye
  vurmadığını, 20 yeniden üretimde aynı HDR ve tahsis boyutunu denetler.
  GPU optik tamponları okunur. Dolu tampon negatif kontrol olarak kullanılır.
- Gizli ve odaksız GLFW bağlamında gerçek sahne yükleme işçisi, CUDA-GL
  piksel eşitliği, sentetik uçuş girdisi, Tab ve GL kaynağının kapatılması
  sınandı. Gerçek fare hareketinin hissi ve monitördeki sunum hızı açık.
- Geometri ve ışık testleri görünümün sanatsal değerlendirmesini kapatmaz.
  Özellikle çok yakındaki kaya ve bitkide sentetik görünüm kalıyor.
