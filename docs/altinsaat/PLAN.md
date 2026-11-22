# BAŞLA — stress_test_pc · "Altın saatte cam ve taş"

> **Bu dosya yeni oturumun promptudur.** Önceki oturumda (2026-10-01) Özcan'la konuşularak yazıldı; araç zinciri
> kuruldu ve uçtan uca doğrulandı. Bağlamın geri kalanı burada: başka yere bakmadan işe başlanabilir.
> Bağlam sıkıştırılırsa bu dosyayı ve `docs/DURUM.md`'yi (sen tutacaksın) yeniden oku.

---

## 1 · Görev, tek cümle

**Özcan'ın bilgisayarında ve monitöründe, gözle görülebilecek zirve grafiği üret:** tek bir sahne, neredeyse durağan,
"bu bilgisayar ve bu monitörde teknik olarak varılabilecek en etkileyici görüntü budur" dedirtecek kalitede.

Özcan'ın kendi sözleri:
- *"Tek bir dar hedefe odaklanalım. Max görüntü-grafik kalitesi."*
- *"Bilgisayar ve monitörümde zirve budur, teknik olarak al gözümle görebileceğim en etkileyici grafik seviyesi budur noktası."*
- *"Neredeyse durağan olabilir, amaç max grafik kalitesi."*
- *"Amaç hem en iyi görüntüye ulaşmak, şaşırmak. Hem de senin sınırlarını görmek."*
- *"İşin analizi, menüsü o su bu su çok önemli değil. Sen tabii ki arkada istediğin analizi-testi yapabilirsin. Benim
  amacım bu sefer test yazılımı yapmak değil, direkt o görsel kaliteyi ve senin sınırını görmek."*
- *"İstediğin aracı, özelliği kullanabilirsin, her şey serbest. İstersen bir araç kur-indir-kullan."*
- GitHub **yok**; yerel git var. Beğenilirse sonra başka sahneler gelecek.

**Başarı ölçütü:** Özcan ekranın karşısına geçip şaşırır. Ölçülebilir yanı (§8): durağan modda referansa yakınsayan,
gürültüsüz, kenar yumuşatmalı, fiziksel olarak doğru ışık; canlı modda 1440p'de 20-30 fps, titremesiz.

## 2 · Neden bu sahne ve neden bu yol (önceki oturumdaki akıl yürütme)

- Oyunlar (Cyberpunk Overdrive, Alan Wake 2 path tracing) ışığı piksel başına 1-2 ışın + yoğun gürültü gidermeyle
  hesaplar, RGB'de çalışır, kostiği ve dağılımı taklit eder ya da yapmaz. Dar ve kapalı bir sahnede bütün RTX 4080'i
  ışığa verirsek, **ışığın doğruluğu ve temizliğinde** onları geçebiliriz. İçerik genişliğinde (insan, animasyon, dev
  dünya) geçemeyiz; o yüzden sahne varlık (asset) değil **ışık ve malzeme** üzerine kurulu.
- Kesişim: Claude'un en güçlü olduğu yer (ışık taşıma matematiği, gölgelendirici, ölçüm) × en etkileyici görüntü
  = **ışık**: gerçek kostikler, spektral dağılım (prizmadan gökkuşağı), ince film girişimi (sabun köpüğü),
  çok saçılımlı hacimsel ışık, fiziksel alan derinliği ve hareket bulanıklığı.
- **Monitör belirleyici** (§4): IPS, 1000:1, yerel karartma yok → karanlık sahne değil, **aydınlık, gün ışıklı,
  doygun renkli ışık**. Altın saat mimari iç mekân bu panelin en iyi göstereceği şey.
- Mimari basit geometri, malzemeler (sıva, beton, su, cam) koddan inandırıcı: fotogerçekçilik için taranmış
  varlık gerekmiyor.

## 3 · Özcan ve çalışma biçimi

- 🔴 **Çalışma kipi: OTONOM, proje bitene kadar.** Özcan (2026-10-01): *"Yeni oturumda grafik devam yazayım, sen
  projeyi bitirene kadar çalış."* Tetikleyici **"grafik devam"**. F0'dan F5'e kendin ilerle; faz sonlarında ekran
  görüntüsünü gönder ama **onay bekleyip durma**. Yalnız gerçekten Özcan'ın vermesi gereken bir karar varsa sor;
  yanıt gelmezse en iyi yargınla devam et ve kararı `docs/DURUM.md`'ye yaz. Bitirince: gösteri, `docs/SINIRLAR.md`
  raporu, hafıza hasadı.

- İletişim **Türkçe**. Hafıza deposu `C:/dev/claude_memory/hafiza/`; indeksi `MEMORY.md`.
- **İşe başlamadan oku:** hafızadaki `genel/feedback_gpu_ve_grafik.md` (bir önceki grafik projesinin dersleri:
  profil önce, önizleme ürünle aynı kaynaktan, kare süresi zaman damgasından, SDF alt sınırı, cihazda görmeden
  bitmez) ve `genel/feedback_olcum_ve_denetim_kulturu.md`.
- **Sanat yönetimi Özcan'da:** her faz sonunda ekran görüntüsünü `SendUserFile` ile gönder, kısa sor. Önemli
  görsel kararlarda (örneğin iki kompozisyon) `AskUserQuestion` ile görsel seçenek sun. Az ama yerinde sor.
- İş özetleri **"NE DOĞRULANMADI"** ve **"Hafıza itildi: evet/hayır"** ile biter. Uzun çizgi (em dash) kullanma.
- 🔴 **Commit kuralı:** test/doğrulama ile commit **aynı komut satırında durmaz**. Önce koş, çıkış kodunu oku, sonra
  ayrı çağrıda commit. (Bu kural önceki projede iki kez çiğnendi; hafızada ders olarak duruyor.)
- Commit mesajları Türkçe, sonunda oturumun verdiği atıf satırları. GitHub yok: `git push` yok.
- Proje hafızası: `hafiza/grafik/project_pc_graphics.md` (not defteri). Kilometre taşlarında güncelle, it.
- **Sınırlar günlüğü:** Özcan Claude'un sınırlarını görmek istiyor. `docs/SINIRLAR.md` tut: nerede zorlandın, hangi
  denemen başarısız oldu, neyi göremedin/ölçemedin. Dürüst ve kısa. Proje sonunda Özcan'a bunu da anlat.

## 4 · Ölçülmüş zemin *(2026-10-01, doğrulandı)*

**Donanım:** NVIDIA GeForce RTX 4080 16 GB (AD103, SM 8.9, 76 SM, RT çekirdeği v30), sürücü 617.14, güç sınırı
320 W (en çok 370 W), boşta ~57 W. CPU AMD Ryzen 9 7900X (iGPU da var: Radeon; ayrıca sanal ekran sürücüsü
"SudoMaker" var, Sunshine/Apollo yayını için; uygulamanın 4080'de koştuğunu doğrula). Disk C: ~147 GB boş.

**Monitör: LG 32GP750-B** — 31,5" IPS, **2560×1440**, 165 Hz (masaüstü 164 Hz), 400 nit, **1000:1**, DisplayHDR 400
(**yerel karartma yok**), %99 sRGB, FreeSync Premium / G-Sync uyumlu. Masaüstü 2560×1440'ta; Özcan DSR 4K'yı yalnız
oyunlarda kullanıyor (DSR = 4K çizip küçültmek: pahalı kenar yumuşatma, ekrana piksel eklemez).

**Bundan çıkan tasarım kuralları:**
- Çıktı **SDR sRGB**, doğal çözünürlükte (2560×1440); kenar yumuşatma piksel başına çok örnekle (DSR'ye gerek yok).
- Ton eşleme **AgX** (Blender'ın; doygun ışıkta ACES'ten iyi, gökkuşağı kostiklerini bozmaz), sonra sRGB OETF,
  sonra **mavi gürültü titreşimi (dither)**: 8 bit IPS'te yumuşak gradyanlar bantlaşır.
- Derin siyaha dayanma: 1000:1'de griye kaçar. Sahne yüksek tonlu.
- 20-30 fps 165 Hz'te VRR aralığının altında: kamera **çok yavaş** ya da durağan; hareket varsa gerçek hareket
  bulanıklığı (obtüratör boyunca zaman örneklenmiş ışınlar).

**Araç zinciri (kuruldu, `tools/smoke` ile uçtan uca doğrulandı — `SMOKE OK`):**
| Parça | Yer | Not |
|---|---|---|
| CUDA 13.4.2 (nvcc, libnvvm, cudart, crt, cccl, NVRTC, NVML, NVTX) | `third_party/cuda` | NVIDIA redist arşivleri: kurucu yok, **sürücüye dokunmaz** |
| OptiX 9.1.0 başlıkları | `third_party/optix/include` | github.com/NVIDIA/optix-dev; çalışma zamanı sürücüde |
| GLFW 3.4 (Win64) | `third_party/glfw` | `lib-vc2022` |
| stb_image_write / stb_image, tinyexr + miniz | `third_party/stb`, `third_party/tinyexr` | PNG ve EXR |
| MSVC 14.51 (VS 2026 Community + Build Tools) | `C:\Program Files\Microsoft Visual Studio\18\Community` | `VC\Auxiliary\Build\vcvars64.bat` |
| CMake + Ninja | VS içinde: `...\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}` | PATH'te değil |
| Python 3.14, numpy, pillow, moderngl | sistem | analiz betikleri |

`third_party/fetch.sh` yeniden çalıştırılabilir (eksik olanı indirir). `third_party/` git'te değil.

**Smoke testin söyledikleri:** nvcc + VS 2026 derliyor · `nvcc -optix-ir -arch=sm_89` OptiX-IR üretiyor, modül
yükleniyor, boru hattı kuruluyor, 2560×1440 raygen doğru yazıyor · `optixInit` 9.1.0, RT core v30, en çok iz
derinliği 31 · **OptiX AI gürültü gidericisi sürücüde: HDR, AOV, TEMPORAL, TEMPORAL_AOV, UPSCALE2X hepsi
oluşturulabiliyor** (2560×1440'ta scratch 614-931 MB) · NVML güç okuyor · OptiX 9.1'de SER (`optixReorder`,
`optixHitObject*`) ve yerleşik küre (`OPTIX_PRIMITIVE_TYPE_SPHERE`) var.
Derleme tuzakları (çözüldü): bağlamada `advapi32` gerekir (OptiX sürücüyü kayıt defterinden bulur); `params`
`extern "C" { __constant__ Params params; }` biçiminde yazılır; vcvars "vswhere bulunamadı" der, zararsız.
Komut: `cmd /c tools\smoke\build.bat`.

## 5 · Sahne: "Altın saatte cam ve taş" — sanat yönetimi

**Ruh:** Tadao Ando (Işık Kilisesi, Koshino Evi, Su Tapınağı), James Turrell'in ışık odaları, Olafur Eliasson'un
prizma ve renk işleri. Sessiz, ağır, ışığın kendisinin konu olduğu bir mekân. Dini simge **yok** (Ando'nun haç
yarığı yerine yatay ve dikey ışık yarıkları).

**Mekân:** ~12 × 7 m, tavan ~6 m, kalıp izli **beton** (Ando imzası: 1800×900 mm panolar, panoda 6 kalıp bağlantı
deliği, ince derzler, hafif ton farkları) ve **beyaz sıva** yüzeyler. Zemin **cilalı açık gri taş/terrazzo**: hafif
yansıtıcı, yer yer pürüzlülük farkı. Bir duvar boyunca **sığ bir su havuzu** (~15-20 cm derin, siyaha yakın değil,
taş tabanlı, durgun ama çok hafif dalgalı).

**Işık:** Güneş alçakta (**yükseklik ~8-12°**), sıcak altın; uzun yatay bir pencere yarığı ve dar dikey bir yarıktan
girer. Duvarda ve zeminde **keskin kenarlı ışık bıçakları**. Havada çok hafif pus ve **toz zerreleri**: huzmeler
görünür. Gökyüzü açıklıklardan görünür (fiziksel atmosfer, gün batımına yakın renk).

**Ana karakterler (her biri oyunların yapamadığı bir ışık olayı):**
1. **Cam prizma** (yoğun flint camı, ör. SF11: dağılımı güçlü) taş bir kaide üstünde, ışık bıçağının içinde →
   beyaz sıva duvara **spektral gökkuşağı kostiği**.
2. **Su dolu cam küre** → yakınsak kostik, zeminde parlak bir ışık odağı.
3. **Havuz yüzeyi** güneşi tavana ve duvara yansıtır → **tavanda dalgalanan ışık desenleri** (yansıma kostikleri).
4. **Birkaç sabun köpüğü** ışık huzmesinde süzülür → **ince film girişimi**, yanardöner renkler, yansımalar.
5. **Hacimsel ışık** → huzmeler, havada asılı toz.
6. (İsteğe bağlı) Pirinç ya da bronz küçük bir nesne → sıcak metal yansıması.

**Kamera:** göz hizası, 35-50 mm (tam kare eşdeğeri), odak prizmada, f/2.8-5.6, 9 bıçaklı diyafram (bokeh).
Kompozisyon: ışık bıçağı, prizma ve gökkuşağı tek karede. **Durağan mod:** sabit kamera, sonsuza yakınsar.
**Canlı mod:** kamera çok yavaş kayar (saniyede birkaç cm) ya da sabit; su dalgalanır, köpükler süzülür, toz yüzer,
güneş çok yavaş döner.

İlk kompozisyonu iki-üç kamera açısıyla Özcan'a göster, seçtir.

## 6 · Teknik mimari

**Dil ve yapı:** C++20 ana program + CUDA/OptiX cihaz programları. CMake (VS'nin CMake'i ve Ninja) + CUDA dili.
Cihaz kodu derlemede `nvcc -optix-ir` ile; **sıcak yeniden yükleme** için NVRTC ile çalışma anında da derlenebilir
(`nvrtcGetOptiXIR`): `.cu` düzenle, F5, sahne yeniden derlensin. Sanat yinelemesini hızlandırır.

**Spektral path tracer:**
- **Kahraman dalga boyu örneklemesi** (Wilkie ve diğ. 2014): yol başına 4 dalga boyu, 380-720 nm, tabakalı. Dağıtıcı
  bir ara yüzde ikincil dalga boyları düşer, yalnız kahraman devam eder.
- Renk: spektrum → CIE 1931 XYZ (Wyman-Sloan-Shirley 2013 analitik eşleme fonksiyonları) → doğrusal sRGB/Rec.709 →
  pozlama → **AgX** → sRGB OETF → mavi gürültü dither → 8 bit.
- Malzeme yansıtırlıkları çoğunlukla nötr: doğrudan yumuşak spektral eğrilerle tanımla. RGB'den spektruma gerekirse
  Jakob-Hanika 2019 sigmoid yöntemi.
- **Güneş:** güneş dışı spektrum × alçak güneşte atmosfer geçirgenliği (Rayleigh + Mie + ozon, hava kütlesi ~5-6) →
  fiziksel olarak sıcak renk. 0,53° disk, kenar kararması. NEE ile koni örneklemesi.
- **Gökyüzü:** açılışta spektral atmosfer (tek + çoklu saçılım, Hillaire 2020 yaklaşımı) ortam haritasına hesaplanır;
  önem örneklemesi (2B CDF / alias tablosu). Güneş çok yavaş dönerse arada yeniden hesaplanır.
- **NEE + MIS** (güneş, gök), Rus ruleti, SER (`optixReorder`) malzemeye göre.
- **Malzemeler:** sıva (Oren-Nayar + ince normal gürültüsü) · beton (prosedürel kalıp izi, delikler, ton farkı,
  mikro normal) · cilalı taş (GGX + clearcoat, pürüzlülük haritası) · su (dielektrik n≈1,333 dağılımlı, Beer soğurması,
  dalga normali: Gerstner toplamı ya da küçük FFT) · cam (Sellmeier: SF11 / BK7) · ince film (Airy formülü, spektral,
  kalınlık yüksekliğe göre incelir) · metal (karmaşık IOR, spektral).
- **Geometri:** mimari üçgen ağları kodla üretilir (C++), detay (kalıp delikleri, derzler, yarık kenarları) geometride.
  Köpükler ve su küresi OptiX'in **yerleşik küresi**. Hepsi tek GAS/IAS.
- **Kostikler (asıl gösteri):** yol izleme güneş→cam→sıva→kamera yollarını (SDS) bulamaz. Ayrı bir **foton geçişi**:
  güneşten, cam/su nesnelerinin sınır kutularına yöneltilmiş spektral fotonlar; aynasal zincir boyunca izlenip ilk
  dağınık yüzeyde biriktirilir. **SPPM** (aşamalı foton haritası): durağan modda yarıçap küçülür, sonuç yakınsar.
  GPU'da hash ızgarası. ⚠️ Çift sayma: yol izleyicide "dağınık yüzeyden aynasal zincirle güneşe varan" katkılar
  dışlanır, onlar fotonların.
- **Hacim:** odada homojen ince pus; huzmeler için kamera ışını boyunca **eşaçılı örnekleme** + güneşe NEE. Toz:
  yerleşik küreler ya da hacim yoğunluğunda gürültü. (Hacimsel kostik: zaman kalırsa foton ışınları.)
- **Kamera:** ince mercek, çokgen diyafram (9 bıçak), odak, pozlama (EV), obtüratör süresince zaman örneklemesi.

**Birikim ve gürültü giderme:**
- **Durağan mod:** saf aşamalı ortalama → gerçeğe yakınsar (hedef: birkaç saniyede gürültüsüz, dakikalar içinde
  referans). Gürültü giderici gücü örnek sayısı arttıkça sıfıra iner.
- **Canlı mod:** zamansal yeniden izdüşüm + **OptiX TEMPORAL_AOV gürültü gidericisi** (albedo + normal + akış
  kılavuzlarıyla). **Kostik tamponu ayrı tutulur** ve gürültü gidermeden sonra eklenir: gürültü gidericiler kostiği
  ve gökkuşağını yayar.
- İsteğe bağlı: düşük iç çözünürlük + UPSCALE2X (yalnız fps yetmezse).

**Ekran:** GLFW, 2560×1440 kenarlıksız tam ekran, birincil monitör. Başta en basiti: cihaz → sabitlenmiş (pinned)
ana bellek → `glTexSubImage2D` (14,7 MB/kare, 30 fps'te ~450 MB/s, sorun değil); gerekirse CUDA-GL birlikte çalışması.
**Arayüz en az:** H göstergeyi aç/kapa (fps, ms, spp, W) · Boşluk durağan/canlı · S ekran görüntüsü (EXR doğrusal +
PNG) · R birikimi sıfırla · F5 gölgelendiricileri yeniden derle · 1-6 hata ayıklama görünümleri (albedo, normal,
kostik tamponu, gürültü, ısı haritası) · ok tuşları pozlama/odak · Esc çıkış. Menü yok.

## 7 · Fazlar ve kapılar

Her faz: kodla → ölç (§8) → ekran görüntüsü al, kendin bak → Özcan'a gönder → `docs/DURUM.md` güncelle → commit.

| Faz | İçerik | Kapı |
|---|---|---|
| **F0 · İskelet** | CMake projesi, GLFW penceresi, OptiX bağlamı, boş sahne, birikim tamponu, AgX + dither, ekran görüntüsü, NVML/zaman ölçümü, NVRTC sıcak yükleme | Pencere 2560×1440'ta açılır, F5 çalışır, ekran görüntüsü EXR+PNG yazar |
| **F1 · Işık doğru** | Spektral çekirdek, oda geometrisi (kutu + yarıklar), sıva/beton/taş, güneş + spektral gök, NEE+MIS | Beyaz fırın testi geçer; D65 beyazı sRGB beyazına çıkar; durağan kare yakınsar; ışık bıçağı keskin ve doğru renkte |
| **F2 · Malzeme ve detay** | Kalıp izli beton, cilalı zemin, havuz suyu, prizma ve küre (dağılım), köpükler (ince film), metal | Sellmeier IOR'ları Fraunhofer çizgilerinde doğru; köpük renkleri bilinen sıraya uyar |
| **F3 · Kostik ve hacim** | Spektral foton geçişi + SPPM, havuz yansıma kostikleri, pus + huzmeler + toz | Duvarda gökkuşağı, tavanda su desenleri, zeminde küre odağı; çift sayma yok (fotonları kapatınca kostik kaybolur, gerisi aynı) |
| **F4 · Canlı mod** | Animasyon (su, köpük, toz, güneş), zamansal birikim, TEMPORAL_AOV, ayrı kostik tamponu, alan derinliği, hareket bulanıklığı | 1440p'de 20-30 fps; titreşim ölçüsü düşük; Özcan gözüyle onaylar |
| **F5 · Sanat ve zirve** | Özcan'la kompozisyon ve ışık turu, referans karşılaştırması, son cila, **gösteri** | Özcan'ın "şaşırdım" ya da somut düzeltme listesi; `docs/SINIRLAR.md` raporu |

## 8 · Ölçüm ve doğrulama (arkada, Özcan'a menü olarak değil)

- **Zaman:** her geçiş için CUDA olayları (yol izleme, foton, gürültü giderme, ton eşleme, aktarım); fps değil ms.
- **Güç:** NVML ile W, saat, sıcaklık CSV'ye. (Path tracing 4080'i güç sınırına dayar: yan ürün olarak stres testi.)
- **Doğruluk testleri:** beyaz fırın (enerji korunumu) · D65 → sRGB beyazı · Sellmeier değerleri · ince film renk sırası ·
  karşılıklılık · NaN/Inf sayacı (bir tane bile olmamalı).
- **Yakınsama:** durağan modda 16k+ spp **referans** EXR. Canlı modun referansa uzaklığı **NVIDIA FLIP** ile
  (`pip install flip-evaluator`, algısal ölçü) ve RMSE ile.
- **Zamansal kararlılık:** sabit kamerada ardışık kareler arası fark (titreşim ölçüsü); hareketli şeylerin dışında
  sıfıra yakın olmalı. Hareketi göremiyorsun: titreşimi sayıyla ölç, kare dizisi kaydet, kareleri yan yana incele.
- **Gözle:** her değişiklikten sonra ekran görüntüsü, kendin bak. Kostik, gökkuşağı ve köpük gibi ince şeylere
  yakından (kırpıp büyüterek) bak.

## 9 · Riskler ve geri dönüş yolları

| Risk | Yol |
|---|---|
| Gürültü giderici kostiği/gökkuşağını yayar | Kostik tamponu ayrı, gürültü gidermeden sonra eklenir; durağan modda gürültü giderici söner |
| SPPM bellek/zaman | Foton sayısını ölç ve ayarla; hash ızgarası; durağan modda kare başına az foton, çok kare |
| 20-30 fps tutmaz | Önce profil (§8); yol uzunluğu, spp, foton sayısı; son çare iç çözünürlük + UPSCALE2X. Durağan mod zaten zirve |
| Spektral renkte hata (pembe beyazlar vb.) | F1'deki beyaz testleri kapı; CMF ve OETF'yi bağımsız test et |
| Çift sayılan kostik ışığı | F3 kapısı: fotonları kapat-aç farkı yalnız kostik bölgelerde olmalı |
| Sahne "boş" görünür | Sanat sorusu: Özcan'a 2-3 seçenekle sor; detay (kalıp delikleri, derz, toz) ölçeği belirler |
| CUDA/VS uyumsuzluğu çıkarsa | Şu an yok (smoke OK); olursa `-allow-unsupported-compiler` ya da `third_party/fetch.sh`'te CUDA sürümü |
| OpenGL iGPU'da açılırsa | Monitör 4080'de; yine de bağlamın `GL_RENDERER`'ını günlüğe yaz |

## 10 · İlk adımlar (yeni oturumda)

1. Hafıza: `git -C C:/dev/claude_memory pull`; `hafiza/genel/feedback_gpu_ve_grafik.md` oku.
2. `cmd /c C:\dev\stress_test_pc\tools\smoke\build.bat` → `SMOKE OK` gör (zincir hâlâ sağlam mı).
3. `docs/DURUM.md` ve `docs/SINIRLAR.md` aç; F0'a başla.
4. Özcan'a başlarken bir cümle: neyin ilk görüleceği (F1 sonunda ilk ışık bıçağı).
