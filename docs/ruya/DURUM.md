# Rüya · Sahne 14

`build\pc_graphics.exe --scene ruya` sahneyi açar. Galeride **14 Rüya**.
**5** açık bahçeleri yukarıdan, **6** revak ve avluyu yakından gösterir.
WASD ve fare uçuşu yönetir. Boşluk ve C yükselme ve alçalma, Shift hızlı
uçuş, Tab imleci bırakma ve alma içindir. Fare dönüşü 0,85 rad/s ile sınırlı.
Dikey görüş açısı tekerlekle 36°–48° arasında değişir.

## Dünya ve görüntü

On altı üretim kalıbı kireçtaşı kütleleri, manastırlar, açık bahçeler ve kaya
kuleleri kurar. Varsayılan ayrıntıda ölçülen kalıp geometrisi **7.629.802
benzersiz üçgen**. Yerleştirilmiş üçgen sayısı bu ağların sahnedeki
kopyalarını sayar. Kayalar, derzler, yapraklar, bronz ve havuz yüzeyi
prosedüreldir. Galeri görseli sahnenin kendi çizdiği karedir.

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
malzeme yolları OptiX üzerinde çalışır. Gözenek ve ince kabartma ayak
izine göre süzülür. AOV gürültü giderici mekânsaldır ve kare geçmişi
taşımaz. Sis yaklaşık yükseklik saçılmasıdır. Havuz opak yansıtıcı bir
yüzeydir. Su iletimi, kostik ve hacimde gölge izleme uygulanmadı.

## Ölçüm düzeni

RTX 4080, sürücü 621.01, 16.376 MiB. Kayıtlarda CUDA olay süreleri ve
CPU duvar saati ayrı. Toplam kare süresine görünür hücre seçimi, geçici
ışık tablosu, çizim ve gürültü giderme dahil. Monitör sunum süresi ayrı
bir ölçümdür. Ölçüm sırasında pencere veya ses açılmadı.

```powershell
cmd /c build.bat
.\build\pc_graphics.exe --scene ruya --test-live
python tools/ruya_verify.py --self-test
python tools/ruya_verify.py --mode all --frames 300 --warmup 30 --repeats 3 --temporal-spp 512
```

Python ölçümleri NumPy, Pillow ve OpenEXR kullanır. FLIP kuruluysa görüntü
farkı ayrıca kaydedilir. Her koşu komutu, çalıştırılabilir dosyanın karması,
kaynak karmaları, GPU bilgisi, ham kare süreleri ve çıkış kodu manifestte.
Başarı eşiği tutmazsa araç çıkış kodu 1 verir ve verileri saklar.

## Son sürümün ölçümü · 2026-11-07

Son kaynak ve çalıştırılabilir dosya ile ölçüm `captures/ruya/final_stratified/`.
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
