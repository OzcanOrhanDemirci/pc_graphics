# PLAN · sahne 11 "Orman"

> Tek kaynak plan. Durum ve ölçümler `docs/orman/DURUM.md`, varlıkların kaynağı ve lisansı `docs/orman/VARLIK.md`
> (ikisi de O0'da açılır). Hafızada görev kartı `grafik/project_orman_gorevi.md`; önce `genel/feedback_gpu_ve_grafik.md`
> §19-21, §27, §30 ve `grafik/feedback_grafik_netlik.md` okunur.
>
> **Yetki (Özcan, 2026-10-27):** *"Bu projede her sahnede belirttiğim gibi tamamen özgürsün. Yani plana birebir sadık
> kalmak zorunda değilsin. En iyi sonucu elde etmek için özgürce planda değişiklik yapabilir, benim kararlarımı da
> değiştirebilirsin. Değişmeyen tek şey hedefimiz."* Plan ya da karar değişirse gerekçesi DURUM'a yazılır.

## 0 · Hedef (değişmez)

**Gerçek bir fotoğraftan ayırt etmekte zorlanılan seviyede, WASD ile içinde gezilen bir yağmur ormanı.**

Özcan: *"Unreal engine is it real memeleri vardır ya [...] Bu gerçek hayattan bir ormanın görüntüsü mü seviyesinde."*
Çıta: ARK Survival Evolved (*"çok güzel görünür ama yapay hissettirir. Temiz bir görüntüsü vardır"*) ve Indiana Jones
and the Great Circle (*"Işıklandırmayı çıkarttığın anda sihir biter"*) ormanları; *"biz daha da iyi olmalıyız. En kötü
öyle olmalıyız."*

## 1 · Kararlar (Özcan, 2026-10-27)

| Konu | Karar |
|---|---|
| Odak | **Yoğunluk.** *"Sonsuz yoğunluk değil, gerçek hayattaki ormanların o kaotikliği ve her yerde bir şeyler var hissi. Yani sahne haline getirilmiş yoğunluk."* Oyun ormanlarının 3-4 malzemesi (uzun ağaç, kısa örtü, odun, toprak) yerine her şey |
| Tür | **Jungle, forest değil:** tropikal yağmur ormanı. Bilimsel doğruluk değil estetik; coğrafyaya bağlı değil |
| Alan | **~30 m.** *"Alanın genişliği değil küçük bir alanın yoğunluğu önemli."* |
| Işık | **RTX ile pişmiş.** Canlıda yalnız canlılığı sağlayan: *"bütün ışığı canlı hesaplamak yerine canlılığı sağlayanı canlı hesaplayalım"* (ıslak parlama, su yansıması, damla, pus) |
| Fizik | **Pişmiş, minimum.** Önceden ayarlanır; **rüzgâr yok** |
| Doku | **~2K.** Performans dokuya değil yoğunluğa |
| Varlıklar | **Açık lisanslı hazır varlıklar.** *"Sen sıfırdan çizme. Her şeyi açık kaynak lisanslardan al. [...] Hiç bulamadığın veya almanın daha kötü sonuç vereceği yerde sen yaparsın, son çare. Hatta mümkünse hiç yapmazsın."* Bizim işimiz yerleşim, ışık, malzeme ve çizim |
| Görüntü | Kaliteli, çok düzgün kenar yumuşatma. **Gren yok, titreme yok, bulanıklık yok** (netlik kuralı) |
| Hareket | **Serbest gezme, WASD**, 1440p'de **30-40 FPS** |
| İlk açılış | Pişirme **15 dakikaya kadar** kabul; sonrası önbellekten |
| Saat, hava | Claude'a bırakıldı → **yağmurdan sonra erken sabah** (§3.6) |
| Kamera | "Elde kamera" hissi seçenek: *"Temiz, bulanık olmayan bir görüntü olacaksa kabul."* |
| Ad | **Orman** (kimlik `orman`, galeride 11) |

## 2 · Başarı ölçütleri (ölçerek)

- **Kör test (asıl ölçüt):** aynı boyda ve aynı sıkıştırmada gerçek yağmur ormanı fotoğrafları ile bizim karelerimiz
  karışık sırayla; bakan kişi her biri için "gerçek / bilgisayar" der. Hedef: doğru tahmin oranı yazı tura seviyesine
  yakın. Hakem Özcan ve onun göstereceği kişiler; ek göz olarak kareleri hiç görmemiş bağımsız bir ajan. Her "bilgisayar"
  cevabının sebebi not edilir, revizenin girdisi o olur.
- **Yan yana:** ARK ve Indiana Jones'un aynı türde ormanı (benzer çerçeve, benzer ışık) ile bizim karemiz.
- **Kıpırtı:** `tools/kararlilik.py` ile sabit kamera yolunda; taban Ada (~0,001). Yaprak en zor durum; O1'de ölçülüp
  hedef dürüstçe konur. Rastgele sayı yok, birikim yok; hareket ve durgunluk tek dünya.
- **Kare süresi** (GPU zaman damgası): varsayılan ayarda 1440p 30-40 FPS, serbest gezerken en kötü bakışta da.
- **İlk açılış** ≤ 15 dk (pişirme), önbellekten açılış ölçülür.
- **Bellek:** WDDM sayfalamasına düşülmez (§27); kart 16 GB, hedef ≤ 13 GB.
- **Yoğunluk sayacı** (mottonun ölçüsü): tür sayısı, benzersiz varlık sayısı, kopya sayısı, sahnedeki etkin üçgen
  sayısı, kameranın 3 m çevresinde m² başına ayrı nesne. Oyunların 3-4 malzemesiyle kıyas için tablo.
- **Çakışma sayımı:** kopyaların birbirine ve zemine girmesi sayılır (hedef: gözle görülebilecek hiç çakışma yok).
  Tam siyah piksel sayımı (delik) Denizaltı'daki gibi.

## 3 · Mimari

### 3.1 Neden görüntü de ışın izlemeyle
Denizaltı'da canlı çizim klasik raster idi, çünkü her yüzey benzersizdi ve tessellation'ı ışın izleme yapısına sığdırmak
imkânsızdı (§30). Ormanın geometrisi ise **kopya**: aynı eğreltiotu binlerce kez. OptiX'te kopya bellekte bir kez durur,
sahne milyarlarca etkin üçgen olsa da ışın başına maliyet logaritmik artar; üst üste binen yaprak katmanları raster'daki
gibi tek tek boyanmaz. Böylece **RT çekirdekleri ışığa değil görünürlüğe, yani yoğunluğa çalışır.**

- **Canlı (OptiX 9.1):** piksel başına sabit desenli N görünürlük ışını (deterministik; N = SSAA seviyesi). Vurulan
  noktada: 2K doku, pişmiş ışığın okunması, ıslak parlamanın formülü. Su ve birikintide tek yansıma ışını (+ sığ suda
  kırılma ışını). Çözümleme HDR'de, geniş süzgeçle. Çıktı GL dokusu (sahne sözleşmesindeki `texture()`).
- **Fırın (OptiX 9.1, yükleme adımı):** yol izleme ile ışık (§3.3). `cache/orman/` altına, git dışı.
- **Yaprak kenarı:** saydam dokulu yaprakta üç yol O1'de eşit sürede kıyaslanır: (a) any-hit ile alfa testi,
  (b) 4080'in donanımının hızlandırdığı opacity micromap (OptiX Toolkit'in pişiricisi), (c) alfa kenarını gerçek
  geometriye çevirmek (kesik üçgen; ışın izlemede üçgen ucuz, kenar en net). Hangisi daha net ve daha hızlıysa.
- **Yedek:** O1'de ışın izleme bütçeyi tutmazsa Denizaltı'nın raster hattı (kopyalı çizim, alfa kapsama) aynı varlık ve
  aynı fırınla. Karar ölçümle.

### 3.2 Varlık hattı
- **Kaynaklar** (O0'da taranır, `VARLIK.md`'de her biri: adres, yazar, lisans, boyut, kalite notu):
  - **Poly Haven** (CC0): kayalar, kökler (`root_cluster`, `single_root`), devrik gövde ve kütükler, çalılar, eğrelti,
    yosun, kabuk döküntüsü (`verdant_trail`, `pine_forest` koleksiyonları), zemin dokuları, gökyüzü HDRI.
  - **ambientCG** (CC0): zemin malzemeleri, dağınık yaprak malzemeleri, 30 adet taranmış yaprak atlası (`LeafSet`).
  - **Sketchfab** (CC0 ve CC-BY, indirilebilir): tropikal bitkiler (monstera, palmiye, eğrelti ağacı, bromelya,
    heliconia, philodendron), payanda köklü dev ağaç taraması (ör. "Moreton Bay Fig Tree", b_nealie), yosunlu kütük
    taramaları, PlantCatalog örnekleri. 10-27 taraması: CC-BY'de aranan her türden sonuç var, CC0'da az.
  - **Lisans:** CC0 ve CC-BY (künyeyle) evet; CC-BY-SA gerekirse; **NC ve ND hayır.** Künye `VARLIK.md` + uygulamada
    künye ekranı. Varlıklar git'e girmez: indirme betiği (`assets/orman/fetch.py`, adres + özet), Koy'daki gibi.
- **Kalite kapısı:** her varlık sahneye girmeden bakılır: ölçek gerçek mi, dokuda pişmiş gölge var mı (taramanın
  ışığı albedoya gömülüyse ya temizlenir ya elenir), yaprak alfası temiz mi, üçgen sayısı. Farklı kaynakların renkleri
  tek bir albedo aralığına getirilir (karışık kaynak "kolaj" gibi görünmesin).
- **Dönüştürücü:** glTF/GLB (cgltf), FBX (ufbx), OBJ → kendi ikili biçimimiz; dokular 2K, BC7 + mip (bc7enc). Uzak
  halka için sadeleştirilmiş sürüm (meshoptimizer). Hepsi MIT, `third_party/fetch.sh`'e eklenir.
- **Çeşitlilik:** kopya başına renk ve ton kayması, ölçek, dönüş, ayna; kurumuş ve böcek yemiş yaprak tonu.
- **Son çare kendi üretimimiz:** yalnız hiç bulunamayan ya da alınması daha kötü olan şey (büyük ihtimalle sarmaşık
  ve liana iplikleri: eğri + taranmış kabuk dokusu). Zemin yüksekliği ve dere yatağı bizim, üstü taranmış malzeme ve
  döküntüyle kaplanır.

### 3.3 Pişmiş ışık (iki kat)
- **Varlığın kendi içi** (varlık başına bir kez, kopyalar paylaşır): yaprağın yaprağa gölgesi (ortam kapanması),
  kalınlık (ışığın içinden geçmesi için).
- **Sahnenin ışığı** (konuma bağlı): seyrek 3B ızgara, SH L1 (Denizaltı'nın fırını); zeminde ve yoğun yerde ince, çatıda
  kaba. Milyonlarca kopyaya ayrı ışık haritası sığmadığı için ızgara. Yaprağın arkasından gelen ışık aynı ızgaradan ters
  normalle okunur (arkadan aydınlanan yeşil yaprak).
- **Güneş görünürlüğü:** ayrı, ince 3B ızgara (1 bayt). Çatı aralığından düşen güneş lekeleri doğal olarak yumuşak
  kenarlıdır (güneşin açısal boyu: 20 m yükseklikteki aralıktan ~15-20 cm yarı gölge), bu yüzden ızgara yeterli; güneşin
  parlaması ve ıslak yüzeyin pırıltısı bu ızgara + formülle canlıda (ışın yok).
- **Pus:** pişmiş saçılma hacmi + güneş görünürlüğünden huzmeler (Denizaltı'nın froxel pusu); §30'daki "kapalı hacmin
  ışığı sızar" dersi.
- **Bütçe:** ≤ 15 dk ilk açılış; iki yarı pişirmenin farkıyla yakınsama ölçülür; ateş böceği sınırı en parlak yüzeye
  göre (§30).

### 3.4 Pişmiş fizik = pişmiş ekoloji
Oyun ormanını yapay gösteren şeylerden biri dağılımın rastgele (gürültü) olması. Gerçekte her şeyin yeri bir sebepten:
- **Su:** zeminin akış birikimi → dere yolu, ıslaklık maskesi, çamur, birikintiler, yosun.
- **Işık:** kaba bir ön pişirmeyle çatı açıklığı → ışık seven genç bitki ve otlar aralıkta gürleşir, gölgede seyrek alt kat.
- **Yerleşim:** katman katman (dev ağaçlar → genç ağaç ve palmiye → çalı → eğrelti ve ot → zemin döküntüsü → gövdeye
  tutunanlar: yosun, eğrelti, bromelya), kurallarla (tür kendi ortamını sever, gövdenin dibine yığılır, birbirine girmez).
- **Düşen her şey düşer:** yaprak, dal, tohum, meyve basit bir katı cisim benzetimiyle bir kez bırakılır ve yerleşir;
  derenin önünde, taşın ve kütüğün arkasında yığılır. Sarmaşıklar ip benzetimiyle sarkar ve sarılır.
- Tohum sabit, sonuç önbellekte; canlıda hiç fizik yok.

### 3.5 Canlılık (canlıda, ucuz, deterministik)
Islak yüzeyin bakışa bağlı parlaması · dere akışı (pişmiş akış haritasıyla kayan normal) · birikinti ve derede yansıma ·
yaprak uçlarından düşen damlalar ve birikintide halkalar (döngülü, rastgele sayı yok) · ağır ağır sürüklenen pus ·
güneş huzmesinde uçuşan zerreler. Yapraklar kımıldamaz (rüzgâr yok; yağmur ormanının alt katı zaten durgundur).

### 3.6 Kompozisyon, saat ve hava (Claude'un seçimi)
- **Devrilmiş dev ağacın açtığı boşluk:** dev bir ağaç devrilmiş, çatıda bir delik açmış; ışık oradan pusun içinden
  huzme huzme iniyor, boşlukta genç bitkiler gürleşmiş; devrik gövde yosun, mantar, eğrelti kaplı ve derenin üstünden
  geçiyor. Yanında payanda köklü dev ağaç (odak). Çevrede koyu, sık alt kat. Hem ekolojik olarak doğru (orman böyle
  açılır) hem gözün tutunacağı bir yer.
- **Yağmurdan sonra erken sabah:** güneş alçak (~25°), çatı aralığından; her yer ıslak, damlalar düşüyor, pus yerden
  kalkıyor. Huzme, ıslak parlama ve arkadan aydınlanan yaprak aynı anda.
- **30 m'nin kenarı görünmez:** gezilen alan tam ayrıntılı; çevresinde ayrıntısı azalan bir halka ve pus. Yükseklik de
  sahnenin parçası: çatı 30-40 m, yukarı bakınca katmanlar görünür.
- Değerler okunaklı: koyu ön plan, aydınlık orta plan, pusla açılan arka plan ("yeşil lapa" olmasın).

### 3.7 Kamera ve görüntü
- WASD, göz 1,7 m, kafa sallanması hafif; çarpışma zemin, gövde, kaya ve kütükle (küçük bitkilerin içinden geçilir).
- Pozlama yavaş ve yumuşak kendini ayarlar (karanlık alt kattan aydınlık aralığa bakınca). Ton eşleme film benzeri,
  hafif ışıltı (`core` ışıltı piramidi), hafif köşe kararması.
- **Kapalı:** alan derinliği, hareket bulanıklığı, gren, renk sapması (netlik kuralı).
- **Elde kamera kipi** (seçenek): yürürken ve dururken hafif el titremesi, telefon videosu hissi; bulanıklık yok.
  Varsayılanı Özcan bakınca belirlenir.
- Ayarlar: çözünürlük 1080p · 1440p · 4K; SSAA 1x-16x; fotoğraf 8K 16x ("hesaplanıyor" kartıyla, Denizaltı gibi).

## 4 · Taşlar

- **O0 · Hazırlık:** sahne iskeleti (galeride 11), `DURUM.md` ve `VARLIK.md`; varlık taraması ve ilk liste; başvuru
  panosu (gerçek yağmur ormanı fotoğrafları, yalnız yerelde); kör test düzeneği. ⚪ Sketchfab hesabı (§6).
- **O1 · Yoğunluk tezgâhı:** dönüştürücü + gerçek varlıklarla milyon mertebesinde kopya; ışın sayısı × kopya sayısı ×
  SSAA'ya karşı kare süresi; yaprak kenarının üç yolu (§3.1); raster yedekle kısa kıyas. **Çıktı: bütçe tablosu** (kaç
  kopya, kaç örnek, hangi yol). Plan bu tabloya göre güncellenir.
- **O2 · Zemin ve su:** yükseklik, dere yatağı, akış ve ıslaklık maskeleri, taranmış zemin malzemeleri, birikintiler,
  dere yüzeyi (yansıma + kırılma).
- **O3 · Ekoloji:** kompozisyon (devrik dev, aralık, payanda köklü ağaç), katmanlı yerleşim, düşme benzetimi, sarmaşık,
  gövdeye tutunanlar, çakışma sayımı.
- **O4 · Fırın:** iki katlı ışık, güneş görünürlüğü, pus; önbellek; ≤ 15 dk.
- **O5 · Malzeme ve ıslaklık:** ıslaklık maskeleri, parlama, yaprağın içinden geçen ışık, kopya çeşitliliği, renk
  kalibrasyonu.
- **O6 · Canlılık:** damla, halka, akış, pus, zerre.
- **O7 · Kamera ve görüntü:** gezinme ve çarpışma, pozlama, ton eşleme, elde kamera kipi, ayarlar, fotoğraf.
- **O8 · Kalite turu:** kör test, yan yana kıyas, kıpırtı, kare süresi (en kötü bakışlar), bellek, bağımsız gözden
  geçirme (kod + kareler), düzeltmeler.
- **O9 · Teslim:** galeri resmi, GOSTERI/DEMO, README (EN + TR), künye ekranı, DURUM'un başı Özcan'ın bakacağı yer.

Her taşta kareler Özcan'a gönderilir; ölçümler DURUM'a.

## 5 · Riskler

| Risk | Karşılık |
|---|---|
| Varlıkların kalitesi ve uyumu (farklı kaynak, farklı ışık, farklı renk) | Kalite kapısı, pişmiş gölgeli taramayı eleme, ortak albedo aralığı |
| Tropikal varlık azlığı | Evrensel olanlar (eğrelti, yosun, kök, kaya, kütük) her kaynaktan; tropikal olanlar Sketchfab CC-BY; son çare kendi üretim |
| Yaprak kenarında maliyet ve merdiven | O1'de üç yol ölçülür; geniş süzgeç; uzakta sadeleşen halka |
| Kart belleği | Seyrek ışık ızgarası, BC7, benzersiz varlık sayısı O1 bütçesine göre |
| Pişirme 15 dk'yı aşar | Izgara çözünürlüğü ve örnek sayısı bütçeye göre; ölçülür |
| "Yeşil lapa" (yoğun ama okunmaz) | Kompozisyon §3.6, değer karşıtlığı, bağımsız göz |
| Kamera yakından yaprağın içine girer | Kameranın önündeki yakın düzlem ve çarpışma O7'de denenir |
| CC-BY künyesi unutulur | Künye `VARLIK.md`'den üretilir, uygulamada ekranı var |

## 6 · Özcan'dan gerekenler

- **Sketchfab hesabı ve API anahtarı** (ücretsiz; Sketchfab indirmeyi yalnız oturum açmış hesaba verir). Anahtar depoya
  ve hafızaya yazılmaz: Özcan kendi ortam değişkenine koyar (`SKETCHFAB_TOKEN`), betik oradan okur.
- İsteğe bağlı: ARK ve Indiana Jones'tan orman kareleri (yan yana kıyas için, yalnız yerelde).
- Kör test için bakacak kişiler (O8).
