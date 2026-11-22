# PLAN · sahne 2: "Fener" (Inside tarzı deniz feneri)

> Tek kaynak plan. 2026-10-01'de Özcan'la konuşularak yazıldı. Durum ve ölçümler `docs/fener/DURUM.md`'de (açılınca).
> Bağlam sıkıştırılırsa önce bu dosyayı, sonra hafızada `genel/feedback_gpu_ve_grafik.md` §9-13'ü oku.

## 1 · Görev ve Özcan'ın sözleri

Sahne 1 teknik zirveydi. Sahne 2 **sanat yönetiminin** sınır testi: bilgisayarı zorlamak değil, göze gerçekten hitap eden,
etkileyici bir sahne. Özcan (2026-10-01):

- *"Daha önce teknik anlamda zirveyi görmeye çalışmış ve çalışma yapmıştık. Şimdi işin sanat dizaynı boyutunu görmek istiyorum."*
- *"Burada amaç bilgisayarı zorlamak falan değil. Gerçekten göze hitap eden etkileyici bir sahne kurmak."*
- *"Senin sanat dizaynı anlamında neler yapabileceğini görmek istiyorum. O nedenle bu senin sınır testin de olacak.
  Gerçekten elinden gelenin en iyisini yap. Amaç göze güzel görünmesi."*

Sahne tarifi (birebir):
- *"Kamera biraz uzaktan ağaçların arasından bir nevi bir adadan karşı adaya bakıyor. Arada deniz var ve hafif dalgalı,
  kıpırdıyor, keskin dalgalı biraz."*
- *"Karşı adada ortada deniz feneri var. Etraf çok karanlık ama deniz feneri sağdan sola, soldan sağa ışığının baktığı
  yerleri aydınlatıyor. Ortadan geçerken kameraya da vuruyor ışığı."*
- *"Deniz feneri biraz tepede, önünde arkasında, sağında solunda sık ve gür ormanlar var. Ağaçlar keskin hatlara sahip."*
- *"Deniz fenerinin sağında hafif bir patikayla bağlı ahşap zayıf bir ev var. Kulübe. Kulübenin içinde penceresinden loş
  cılız bir ışık görünüyor."*
- *"Havada ay ışığı var ama etrafı aydınlatmaya yetmiyor. Denizin yüzeyinde hafif bir parıldama, loş bir yakamoz yaratıyor.
  Asıl ışık kaynağı deniz feneri."*
- *"Görsel tarz: Inside oyunu tarzında."* Dredge görseli yalnız fener örneği; tarz Dredge değil.
- *"Görsel tarzda özellikle o yakındaki ve uzaktaki nesnelerin ayrımı çok güzel olmalı. Işıklandırma çok etkileyici olmalı."*
- *"Su orman gökyüzü deniz feneri kulübe patika bunların her biri görünmeli ama apaçık değil, o karanlık atmosferde hafif
  görünmeli. Asıl görüntüyü sağlayan şey, netleştiren şey deniz fenerinin ışığı olmalı. O loşluğu sağlayan da ay ışığı."*
- *"En önemli sanatsal kararlardan biri yüce dediğimiz o uçsuz bucaksızlık hissi. Sonsuzluğa gidiyormuş gibi boşluk hissi.
  [...] Sahnenin arkasında sanki devasa bir dünya varmış da biz kameranın bulunduğu adadan karşı adaya bakarken sadece
  küçük bir kısmını görüyoruz aslında hissi."*

Özcan'ın eskizi `docs/fener/ref/ozcan_eskiz.png` (fener ortada, kulübe sağda, aralarında
patika, ışık dönüyor, kamera alt ortada, arada su). Oyunlardan alınan referans kareleri telifli olduğu için depoda tutulmuyor.

### Özcan'ın kararları (2026-10-01, AskUserQuestion)
| Soru | Karar |
|---|---|
| Çıktı | **Video döngüsü + canlı pencere.** Asıl eser: bir fener turu boyunca kesintisiz başa saran, 1440p işlenmiş video. Canlı pencere de olacak. |
| Figür | **Figür yok.** Kamera izleyicinin gözü; insan varlığını yalnız kulübenin ışığı anlatır. |
| Renk | **Soğuk tek renk + sıcak pencere.** Her şey çelik mavisi-gri, huzme soluk beyaz, tek doygun renk kulübe penceresi (kehribar). |
| Yağmur | **Yalnız huzmede görünen çisenti.** Karanlıkta görünmez, huzme geçerken ışığın içinde ince çizgiler. |

Yorumum (Özcan düzeltmedi): "yakamoz" = ay ışığının suda kırık kırık parlaması (parıltı yolu), deniz canlılarının
ışıldaması değil.

### F0 sonrası kararlar (2026-10-01, taslaklar `docs/fener/etud/` üzerine)
- **Kompozisyon: A "Pencere" başlangıç, B "Yücelik" varış.** C "Yakın"ı beğenmedi. Özcan'ın sözleri: *"A ve B ikisi de
  çok güzel. [...] A da başlıyor. Mesela canlı renderda Space tuşuna basınca kamera yavaşça B ye gidiyor ve o gidiş de
  canlı render, orman içinden ağaçların arasından geçiyor ve adanın diğer tarafına doğru o yüce alana ulaşıyor. Sonra
  tekrar space basınca tekrar A ya doğru yola çıkıyor. Yani canlı render + etkileşim olmuş oluyor."*
- **Asıl ürün artık canlı, etkileşimli pencere.** Space: A → B yolculuğu (40-60 sn, yumuşak kalkış/duruş), tekrar
  Space: geri; yolda basılırsa yumuşakça döner. Video yan ürün (A döngüsü + yolculuk kaydı).
- Kamera adası gerçek 3D orman: kamera ağaçların arasından geçer (yakından inandırıcı köknar + binlerce kopya).
- Durunca (A'da, B'de) görüntü birkaç saniyede durağan kaliteye toplanır; yolda canlı kalite (gürültü giderici).
- **Tepe anı: ışık sütunu + göz kamaşması** (taslaktaki gibi; huzme kameranın üstünden geçer, lamba kör eder,
  köknarlar simsiyah, deniz parlar, sonra yavaşça karanlık).

## 2 · Araştırma: Inside nasıl yapılmış (Playdead'in kendi kaynakları)

Kaynaklar: GDC 2016 "Low Complexity, High Fidelity: INSIDE Rendering" (Gjøl, Svendsen; slayt + konuşmacı notları,
`github.com/playdeadgames/publications/tree/master/INSIDE`), "Turn it Down 90%" (Svendsen, Control 2016, aynı depo),
Playdead blog "The Lighting of INSIDE", Pixel Fondue ve Casual Effects (McGuire) incelemeleri.

1. **Sis + siluet üslubun kendisi.** *"Fog turned out to be very central to our artstyle. Many of the initial scenes in the
   game were literally just fog + silhouettes."* Sissiz kareyi "çok çıplak" buluyorlar. Sis **basit doğrusal** sis.
2. **Parlak şey sisi deler:** sis bir tavanla sınırlı (*"clamp the fog to a max-value to enable bright objects to bleed
   through"*): farlar, projektörler sisin içinden sızar.
3. **İki ayrı parlama:** yarım ekran genişliğinde "tereyağlı" düşük eşikli parlama = atmosferik saçılım ("screen" ile
   karışır); ayrı, dar HDR parlama yalnız ışık kaynaklarında. İkisi birbirini bozduğu için ayrı geçişler.
   Parlama hangi şiddetle hesaplandıysa kaynak piksel de o şiddetle gösterilmeli (enerji tutarlılığı).
4. **"%90 kıs":** yağmur *"rahatsız hissettirene kadar"* kısıldı (*"you wouldn't notice most of the time unless you looked
   in the shade, or under a lamp"*); su, yağmur, köpük ekranın en beyaz şeyi olmamalı; duman opak gri olamaz.
   Gökkuşağı lens parlaması "renkler nereden geliyor?" diye tek renge çekildi. *"Your work is only as good as its worst
   detail."* Ayrıntının ayrıntısı yok: parçacık dokusu neredeyse bir lekeye indirildi.
5. **Gizli renklendirme:** duman/buhar iki renkle boyanır: aydınlık yanı arka planın aydınlık rengi, gölge yanı arka
   planın gölge rengi. Efekt resme karışır, öne çıkmaz.
6. **Işık elle yerleştirilir:** sekme ışığı, kenar ışığı, gölge "çıkartmaları", AO hepsi sanatçının elinde; GI yok.
   Ayrıntı kameraya uzaklığa ve anlatı odağına göre çok hassas ayarlanır.
7. **Bantlanma:** karanlık yumuşak degradeler 8 bitte basamaklanır; her geçişte (ışık, son işlem, G-tampon) titreşim
   (dither), üçgen dağılımlı (TPDF) mavi gürültü. sRGB ara tamponlar.
8. **Hacimsel ışık** yerel sis kutularında, mavi gürültüyle az örnekli ışın yürütme + zamansal birikim. "Gelecek iş:
   projektör için eşit açılı (equi-angular) örnekleme." Biz yol izleyicide bunu baştan yaparız.
9. **Tek renk + tek vurgu:** çocuğun kırmızı tişörtü.
10. **Bir kez taşma hakkı** (*"Turn it up 90%"*): oyun kuralı bir kez bozar.

## 3 · Sanat yönetimi

**Görsel hedef (bir bakışta göz ne görüyor?):** *Uçsuz bucaksız karanlık bir sisin içinde tek bir ışık nefes alıyor.*
Okuma sırası: (1) fener lambası, (2) huzmenin o an süpürdüğü yer, (3) kulübenin küçücük sıcak penceresi, (4) arkadaki
dev boşluk.

**Kompozisyon (eskize sadık):** 16:9. Kamera yakın adada, alçakta (deniz seviyesinden ~5 m), ağaç gövdeleri arasından.
Fener biraz yukarıda: hafif yukarı bakış, yücelik. Ay kadrajda sol üstte, sahne **arkadan aydınlatılmış** (Inside'ın
siluet tekniği). Altı derinlik katmanı, önden arkaya her biri bir ton açık ve soluk:
1. Ön plan (yakın ada): kenarlarda gövdeler, üstten sarkan dallar; neredeyse saf siyah; huzme geçerken ince kenar ışığı.
2. Deniz: koyu, keskin küçük dalgalar; ay parıltı yolu; fener lambasının titreyen dikey yansıması.
3. Karşı ada: tepede fener, sivri tırtıklı köknarlar, sağda patika ve kulübe; sisle yumuşamış koyu gri.
4. Arka adalar ve kayalıklar.
5. Uzak dev: yüzlerce metrelik sırtlar, sisle neredeyse bir; ufukta zar zor seçilen minicik ikinci ışık.
6. Gök: ince bulut perdesinin arkasında ay ve geniş hâle; ufuk çizgisi yok, deniz sisin içinde göğe erir.

**Işık:**
- Fener (anahtar): lamba odası sürekli parlak çekirdek; iki zıt huzme; bir tur 12-16 sn. Huzme sisi görünür kılar,
  ağaçların arasından ışık tarakları çıkarır, denizi süpürüp dalga sırtlarını yakar.
- Kameraya döndüğü an (tek taşma): sis ışığı geldiği yönde güçlü saçar (ileri saçılım, fiziksel): bütün sis aydınlanır,
  ön plan sert siluete döner, lamba yıldız gibi parlar, sonra söner.
- Ay (loşluk): soğuk, zayıf; sise yukarıdan ışık, ağaç tepelerinde ince kenar ışığı, suda parıltı yolu.
- Kulübe: küçük, sıcak, cılız, belli belirsiz titreyen gaz lambası; pencereden sise biraz sızar.

**Renk:** soğuk çelik mavisi-gri, neredeyse tek renk; en parlaklar lamba ve huzme (nötr-soğuk beyaz); tek sıcak, doygun
renk kulübe penceresi. Fenerin kırmızı-beyaz çizgileri yalnız açık-koyu farkı olarak.

**Hareket:** huzme döner, deniz kıpırdar, sis yavaşça sürüklenir, kulübe ışığı titrer, çisenti yalnız huzmede. Hepsi bir
fener turuna kilitli: video kusursuz döngü.

## 4 · Teknik yol

Mevcut motor (OptiX yol izleyici), sahne 1'e dokunmadan: sahne seçimi, her sahnenin kendi `.cfg`'si ve cihaz programı.
Fizik doğruluğu sanatın hizmetinde; Inside gibi bilerek fizik dışı sanat ayarları olacak (örneğin ışık başına sis
saçılım çarpanı, sisin parlak kaynakları sınırlı örtmesi) ve her biri kodda ve DURUM'da **açıkça işaretlenir**.

| Faz | İş | Özcan'a gösterilen |
|---|---|---|
| F0 | Değer etüdü: 3D'den önce 3 kompozisyon taslağı (2B, Python) | 3 taslak, Özcan seçer |
| F1 | Altyapı: sahne seçimi; sahne 1 aynen çalışıyor (önce/sonra karşılaştırması) | |
| F2 | Kuşbakışı harita (iki ada, A, B, orman içi yol) → kaba 3D: araziler, orman (tek modelden binlerce kopya), fener, kulübe, patika, deniz, sis, ay; A ve B karelerinin kilidi | harita, sonra A ve B ilk kareleri |
| F2b | Yakın köknar (kameranın arasından geçeceği ağaç) ve kamera adası ormanı; Space ile A ↔ B yolculuğu (yol eğrisi, yumuşak hız) | yolculuktan kareler |
| F3 | Işık: dönen huzme, sis içinde lamba örneklemesi (eşit açılı), yükseklikle incelen sürüklenen sis, ay + bulut + hâle, kulübe | huzmenin 3-4 açısı |
| F4 | Su: keskin küçük dalgalar, ay parıltı yolu, lamba yansıması | |
| F5 | Son işlem (Inside zinciri): iki parlama, TPDF mavi gürültü titreşimi, renk derecelendirme, hafif vinyet | son kare adayı |
| F6 | Hareket, döngü, canlı pencere kalitesi (durunca toplanma, yolda gürültü giderici), video (ffmpeg) | canlı + video |
| F7 | Sanat turları: "%90 kıs", ayrıntı silme, Özcan'ın gözü | |

**Ölçülecekler (sanatta ölçülebilir olan):** katman maskeleriyle her derinlik katmanının ortalama parlaklığı önden
arkaya düzenli artmalı (atmosfer perspektifi); 8 bit çıktıda karanlık degradelerde basamak yok; döngünün ilk ve son
karesi aynı; yeni cihaz programı da fırın testinden geçer; sahne 1 değişmedi.

## 5 · Çalışma biçimi

- Sanat kararı Özcan'da; kısa yorumları birçok denemeye bedel (hafıza `genel/feedback_gpu_ve_grafik.md` §13). Erken ve sık kare göster
  (`SendUserFile`), görsel seçeneklerde `AskUserQuestion`.
- Commit kuralı: doğrulama ile commit aynı komut satırında durmaz. Commit mesajları Türkçe, oturumun atıf satırlarıyla.
  Sıra: hafıza → `claude_memory` commit/push → proje commit/push (GitHub `pc_graphics`).
- İş özetleri "NE DOĞRULANMADI" ve "Hafıza itildi: evet/hayır" ile biter. Uzun çizgi kullanma.
