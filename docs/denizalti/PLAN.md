# PLAN · sahne 8 "Denizaltı"

> Tek kaynak plan. Durum ve ölçümler `docs/denizalti/DURUM.md`, kaynak ve envanter `docs/denizalti/REFERANS.md`.
> Hafızada görev kartı `grafik/project_denizalti_gorevi.md`; önce `genel/feedback_gpu_ve_grafik.md` §19-21, §27 ve
> `grafik/feedback_grafik_netlik.md` okunur.
>
> **Yetki (Özcan, 2026-10-06):** *"Tam otonom gidelim [...] bu plan da dahil olmak üzere uymak zorunda değilsin. Tam
> yetkilisin ve özgürsün. Amaca ve en iyi sonuca giden yolda her şey serbest. Bu plan sadece yardımcı bir yön [...]
> Mükemmel bir sonuç bekliyorum. Bitirene kadar devam et."* Plan değişirse gerekçesi DURUM'a yazılır.

## 1 · Kararlar (Özcan, 2026-10-06 sabahı)

| Konu | Karar |
|---|---|
| Yer | Ada'nın kardeşi; çift: 5 Ada (güncel teknolojiyle netlik) · 8 Denizaltı (eski teknikle, dolaylı yoldan) |
| Odak | **Atmosfer.** Fotogerçekçi, *"olabilecek en gerçekçi"*, çıta *"rdr2'nin üzerine çıkmak"*. "Fizik ne derse" mottosu bu sahnede yok: hedef görüntü |
| Işık | **Canlıda ışık hesabı yok.** RTX ile önceden pişirilir, normalde takip edilenden kat kat fazla. Bütün canlı bütçe göstermeye. **Tek ışık** (beyaz/kırmızı karışık), ışık seçimi yok |
| Fizik | Önceden hesaplı ya da en az |
| Mekân | **Tench/Balao sınıfı filo denizaltısının yalnız kontrol odası.** Örnek TCG Uluçalireis (S-338, eski USS Thornback). *"Oyun değil teknoloji demosu"*: bölmeler arası gezme yok |
| Hâl | **Görevdeki tekne, bakımlı ama yaşanmış.** Mürettebat yok |
| Hareket | WASD, küçük alanda (Metro 2033 hissi). Yakından inceleme: yüzeye 10-15 cm, sağ tık dürbün |
| Ayarlar | Çözünürlük 720p · 1080p · 1440p · 4K · 8K; SSAA 1x · 2x · 4x · 8x · 16x; Tessellation, Grafik, Işık, Efekt: Low · Medium · High |
| Seviyeler | High *"abartılı oha seviyesi"*; Medium *"ortalama üstü, tessellation'ı çok iyi kullanan oyun görselliği"*; Low *"kalitesiz değil de daha az yoğun"*, *"detaysız güzel"* |
| 8K 16x | Yalnız fotoğraf, "hesaplanıyor" ekranıyla |
| Yok | Ölçüm paneli (*"Görüntünün mükemmelliğine odaklanalım"*), ışık seçimi, mürettebat, gezilecek başka bölme |
| Ad | **Denizaltı** (kimlik `denizalti`, galeride 08) |

## 2 · Başarı ölçütleri (ölçerek)

- **Kıpırtı:** canlı yolun kareden kareye değişimi eksi yakınsamış referansın değişimi (`tools/kararlilik.py`), sabit
  kamera yolunda. Hedef Ada'nın tabanı (~0,001) ya da altı. Gren hiçbir seviyede yok; rastgele sayı yok; birikim yok.
- **Çatlaksız geometri:** tessellation sınırlarında delik yok (arka plan macenta, sayılır: 0 piksel).
- **Işığın gürültüsü:** pişirmenin yakınsaması ölçülür (iki bağımsız yarı pişirmenin farkı); lekelilik görünmez.
- **Kare süresi** (GPU zaman damgası): varsayılan ayarda (1440p, SSAA 4x, hepsi High) akıcı; her kombinasyon ölçülür,
  yavaş olanlar dürüstçe yavaş. 8K 16x yalnız fotoğraf.
- **Bellek:** her seviye kendi belleğini kullanır (Low seçilince bellek düşer); WDDM sayfalamasına düşülmez (§27).
- **Göz:** "RDR2'nin üstü"nün hakemi Özcan. Ara kareler SendUserFile ile.

## 3 · Mimari

Neden canlıda klasik çizim hattı: **"High = örnek başına üçgen"** yalnız görüşe bağlı tessellation ile mümkün. Işın
izlemede bu geometri belleğe sığmaz: 250 m² yüzey 0,1 mm üçgenle ~2,5·10¹⁰ üçgen; 1 mm'de bile ~2,5·10⁸ üçgen ve
~16 GB'lık yapı. Raster hattı üçgenleri her karede üretip atar, bellekte tutmaz. Bu yüzden:

- **Canlı (OpenGL 4.6, ana iş parçacığı):** donanım tessellation (TCS/TES), yer değiştirme (displacement), çoklu örnekli
  hedefte örnek başına gölgelendirme (gerçek SSAA), pişirilmiş ışığı okuyan ileri gölgelendirme, pişirilmiş pus hacmi,
  çözümleme (resolve), parlama, ton eşleme, titreşim. Çıktı bir GL dokusu (sahne sözleşmesine `texture()` eklenir).
- **Fırın (OptiX 9.1, komut satırı):** yol izleme ile ışık haritası (yönlü, SH L1), yansıma yoklamaları, pus hacmi.
  Sonuç `cache/denizalti/` altında dosya (git dışı). Sahne yüklenirken okunur.
- **Geometri (C++, prosedürel):** parametrik parça kiti (boru, dirsek, flanş, cıvata, vana, el çarkı, gösterge, şalter,
  kablo demeti, perçinli levha, çerçeve, merdiven, lamba...). Her parçanın ışık haritası açılımı (UV2) kodla üretilir.
  Bütün kenarlar pahlı (sert yüzeyin gerçekçiliği kenarın ışığı yakalamasından gelir).
- **Malzeme:** CC0 taramalar (ambientCG, Poly Haven) + geometriden hesaplanan aşınma (kenarda boya kırığı, oyukta kir,
  üst yüzde toz, akıntı izi). Etiket yazıları ve gösterge kadranları kodla (stb_truetype), oyma yazı yer değiştirmeyle.

### 3.1 Tessellation
- Taban ağı ~1-2 cm kenar (yakın parçalar). TCS kenar çarpanını kenarın ekrandaki **örnek** uzunluğundan hesaplar
  (komşu yamalar aynı kenarı aynı hesaplar: çatlak yok); görüş dışı ve arka yüz yamalar 0.
- Yer değiştirme yönü köşede ortalanmış "yer değiştirme normali"yle (sert kenarda bile çatlak yok); gölgelendirme normali
  ayrı. Eğri yüzeyler (boru) Phong tessellation ile yuvarlanır.
- Seviyeler: **High** hedef üçgen kenarı ~1 örnek (tavan: kaynağın ayrıntısı ~0,1 mm, çarpan 64); **Medium** ~6 piksel,
  büyük kabartmalar geometri; **Low** ~24 piksel, ayrıntıyı normal haritası taşır.
- Yükseklik örneklemesi üçgen boyuna uygun mip'le (alt-piksel yükseklik gürültüsü kıpırdatır).

### 3.2 SSAA
- Örnek başına gölgelendirme (sample shading) + çoklu örnek hedef; 16x için ya donanımın 16 örneği ya da 2×2 ölçek ×
  4 örnek (D0'da ölçülür). Örnek deseni sabit (deterministik).
- Çözümleme HDR'de, parlaklığa göre ağırlıklı (yüksek karşıtlıklı kenarda merdiven yok), geniş süzgeçle; ardından ekrana
  kaliteli küçültme/büyütme (render çözünürlüğü ≠ ekran).
- **Fotoğraf:** seçili ayarda, parça parça; 8K 16x dahil. "Hesaplanıyor" kartı + ilerleme; PNG + EXR.

### 3.3 Pişirilmiş ışık
- Işık haritası: yüzey noktasının gelen ışığı SH L1 (yön + renk) olarak; yarı parlak yüzeyler bakış açısına göre doğru
  parlar. High 2 mm/teksel hedef (bellek ölçülür), Medium ve Low High'dan süzülür (yeniden pişirme yok, tutarlı).
- Yol izleme: binlerce örnek, sınırsız sekme (Rus ruleti), küçük ışıklara doğrudan örnekleme. İki yarı pişirmenin farkı
  gürültüyü ölçer.
- Yansıma yoklamaları: odada sık, küp haritası, GGX ön süzme, kutuya göre paralaks düzeltme.
- Pus: 3B hacim (gelen saçılmış ışık), kamera kesitinde birikim (ışık hesabı değil, saklı ışığın toplanması).
- Işık kaynakları: kafesli akkor ampuller, kırmızı gece lambaları, tek beyaz kaynak (yeri sanat etüdüyle).

### 3.4 Kamera
- Fare bakış, WASD, Shift yavaş (yakın inceleme), Boşluk/C yukarı-aşağı (oda içinde, sınırlı). Çarpışma: odanın uzaklık
  alanı (2 cm), kamera küresi 0,12 m. Sağ tık dürbün (görüş 4 kat dar, fare duyarlılığı onunla).

## 4 · Ayarların kapsamı

| Ayar | Low | Medium | High |
|---|---|---|---|
| Tessellation | ~24 px üçgen, normal haritası | ~6 px, büyük kabartmalar geometri | ~1 örnek, her kabartma geometri |
| Işık | 8 mm, yalnız yaygın ışık, tek yoklama | 4 mm, SH L1, seyrek yoklama | 2 mm, SH L1, sık yoklama, keskin temas gölgesi |
| Grafik | dokular ≤ 2K, sade aşınma | 4K, tam aşınma | 8K + prosedürel mikro ayrıntı, oyma etiketler |
| Efekt | düz pus, hafif parlama | pişirilmiş pus (yarım), parlama | tam pus + huzmeler, toz zerreleri, yoğuşma damlaları, göz parlaması |

Low'lar High'dan türer: aynı ışık, aynı dünya, daha az ayrıntı. Gren hiçbir seviyede yok.

## 5 · Taşlar

- **D0 · Hat ve test duvarı:** GL 4.6 yükleyici, sözleşmeye doku yolu, sahne iskeleti (galeri 08), tessellation
  L/M/H + SSAA + çözümleme; test duvarı (gövde kesiti, manifold, borular, vanalar). Ölçümler: GL sınırları, kare süresi
  tablosu, çatlak, kıpırtı. İlk fotoğraflar Özcan'a.
- **D1 · Fırın:** UV2 açılımı, OptiX yol izleme ile yönlü ışık haritası, yoklamalar, pus hacmi; test duvarında.
- **D2 · Parça kiti:** envanterin bütün parça türleri, pahlı, UV'li, yer değiştirmeli.
- **D3 · Oda:** basınç gövdesi, postalar, güverte, tavan, kapılar, envanterin hepsi yerinde (REFERANS'a göre).
- **D4 · Malzeme ve aşınma:** taramalar, boya katmanları, geometri kaynaklı aşınma, etiketler ve kadranlar.
- **D5 · Işık ve atmosfer:** lambalar, son pişirme, pus ve huzmeler, efektler (toz, damla, parlama), pozlama.
- **D6 · Ayar merdiveni ve fotoğraf:** bütün seviyeler, bellek, 8K 16x fotoğraf.
- **D7 · Kalite turu:** kıpırtı ve kare süresi tüm kombinasyonlarda, bağımsız gözden geçirme, galeri resmi, README,
  GOSTERI/DEMO, DURUM özeti.

## 6 · Riskler

- İçerik hacmi sonsuz cilaya açık → sabit envanter listesi (REFERANS) bitince oda biter.
- Ayna gibi yüzeyler (cam kapaklar, krom) pişirilmiş ışığın zayıf yeri → yoklamalar sık, malzeme seçimi bilinçli.
- Mikro üçgende raster verimsizliği (dörtlü piksel gölgelendirmesi) → ölçülür; gerekirse taban ağı ve hedef boy ayarı.
- Bellek 16 GB (WDDM sayfalar) → seviye başına bütçe, `cudaMemGetInfo` / GL bellek sorgusu.
